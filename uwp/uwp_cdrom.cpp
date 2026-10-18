/* Reading the disc in an Xbox console's own drive (Dev Mode, UWP).
 * Copyright (c) 2026 Skitzelles, https://github.com/skitzellez - written for Game Vault.
 * Free software under the GNU General Public License version 3 or later, like RetroArch itself;
 * this notice stays in every copy.
 *
 * An app on the console is refused every file and volume route to the drive (CreateFile on \\.\X: and the
 * device requests that go with it). What does open is the drive's device interface through
 * Windows.Devices.Custom, once the package declares the CD-ROM interface class
 * ({53f56308-b6bf-11d0-94f2-00a0c91efb8b}) as a DeviceCapability; opened for reading and writing it takes
 * IOCTL_SCSI_PASS_THROUGH. libretro-common/cdrom/cdrom.c sends all of its commands through
 * uwp_cdrom_command() below instead of DeviceIoControl.
 *
 * RetroArch runs on the window's thread, where waiting for an asynchronous WinRT call is not allowed, so a
 * worker thread of its own talks to the drive and the caller waits for it. */

#include <winrt/Windows.Devices.Custom.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>

#include <windows.h>

/* From ntddscsi.h, which keeps these away from apps. */
struct SCSI_PASS_THROUGH
{
   USHORT Length;
   UCHAR ScsiStatus;
   UCHAR PathId;
   UCHAR TargetId;
   UCHAR Lun;
   UCHAR CdbLength;
   UCHAR SenseInfoLength;
   UCHAR DataIn;
   ULONG DataTransferLength;
   ULONG TimeOutValue;
   ULONG_PTR DataBufferOffset;
   ULONG SenseInfoOffset;
   UCHAR Cdb[16];
};
enum { SCSI_IOCTL_DATA_OUT = 0, SCSI_IOCTL_DATA_IN = 1, SCSI_IOCTL_DATA_UNSPECIFIED = 2 };

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>

namespace
{
   using namespace winrt::Windows::Devices::Custom;
   using namespace winrt::Windows::Devices::Enumeration;
   using winrt::Windows::Storage::Streams::Buffer;

   const uint32_t SPT_HEADER = sizeof(SCSI_PASS_THROUGH) + 32;   /* the structure, then room for sense data */
   const uint32_t MAX_DATA   = 64 * 1024;                        /* one command moves at most this much */

   struct Drive
   {
      CustomDevice device{nullptr};
      Buffer in{SPT_HEADER + MAX_DATA};
      Buffer out{SPT_HEADER + MAX_DATA};
      bool tried = false;
   };

   /* LocalState\uwp_cdrom.log: what was asked of the drive and what it said (the first few hundred lines of
    * a session), for looking into a disc that does not start. */
   void drive_log(const char *fmt, ...)
   {
      static FILE *f = nullptr;
      static int lines = 0;
      static ULONGLONG t0 = 0;
      /* "disc watch" lines always go in: the reads of a starting game fill the 600 lines in seconds */
      if (lines > 600 && strncmp(fmt, "disc watch", 10) != 0)
         return;
      if (!f)
      {
         try
         {
            std::wstring path = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) + L"\\uwp_cdrom.log";
            f = _wfopen(path.c_str(), L"w");
         }
         catch (...)
         {
         }
         t0 = GetTickCount64();
         if (!f)
         {
            lines = 1000;
            return;
         }
      }
      va_list ap;
      va_start(ap, fmt);
      fprintf(f, "%7llu ms  ", GetTickCount64() - t0);
      vfprintf(f, fmt, ap);
      fputc('\n', f);
      va_end(ap);
      fflush(f);
      lines++;
   }

   /* One worker thread; jobs are run on it one at a time. */
   std::mutex              g_lock;        /* one caller at a time */
   std::mutex              g_job_lock;
   std::condition_variable g_job_cv;
   std::function<void()>   g_job;
   bool                    g_job_done = true;
   bool                    g_worker_started = false;
   Drive                  *g_drive = nullptr;   /* used on the worker thread only */

   void worker()
   {
      winrt::init_apartment();
      for (;;)
      {
         std::unique_lock<std::mutex> l(g_job_lock);
         g_job_cv.wait(l, [] { return !g_job_done; });
         l.unlock();         /* the caller must be able to wait (and look after its window) meanwhile */
         g_job();
         l.lock();
         g_job_done = true;
         l.unlock();
         g_job_cv.notify_all();
      }
   }

   void run_on_worker(std::function<void()> job)
   {
      std::lock_guard<std::mutex> one(g_lock);
      if (!g_worker_started)
      {
         g_worker_started = true;
         std::thread(worker).detach();
      }
      {
         std::lock_guard<std::mutex> l(g_job_lock);
         g_job      = std::move(job);
         g_job_done = false;
      }
      g_job_cv.notify_all();

      /* Called on the window's thread (RetroArch's main loop runs there), the wait keeps the window's
       * events moving: opening the drive does not finish while that thread stands still. */
      winrt::Windows::UI::Core::CoreWindow window{nullptr};
      try
      {
         window = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread();
      }
      catch (winrt::hresult_error const&)
      {
      }
      std::unique_lock<std::mutex> l(g_job_lock);
      while (!g_job_cv.wait_for(l, std::chrono::milliseconds(window ? 5 : 1000), [] { return g_job_done; }))
      {
         if (!window)
            continue;
         l.unlock();
         try
         {
            window.Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
         }
         catch (winrt::hresult_error const&)
         {
         }
         l.lock();
      }
   }

   /* Worker thread. Opens the drive once; a failed open is tried again on the next call. */
   bool open_drive()
   {
      if (!g_drive)
         g_drive = new Drive();
      if (g_drive->device)
         return true;
      drive_log("opening the drive");
      try
      {
         const auto found = DeviceInformation::FindAllAsync(
            L"System.Devices.InterfaceClassGuid:=\"{53f56308-b6bf-11d0-94f2-00a0c91efb8b}\"").get();
         drive_log("drives found: %u", (unsigned)found.Size());
         if (found.Size() == 0)
            return false;
         g_drive->device = CustomDevice::FromIdAsync(found.GetAt(0).Id(),
            DeviceAccessMode::ReadWrite, DeviceSharingMode::Shared).get();
      }
      catch (winrt::hresult_error const& e)
      {
         drive_log("open failed, error %08x", (unsigned)e.code().value);
         g_drive->device = nullptr;
      }
      drive_log(g_drive->device ? "drive open" : "drive did not open");
      return g_drive->device != nullptr;
   }
}

extern "C" int uwp_cdrom_available(void)
{
   bool ok = false;
   run_on_worker([&] { ok = open_drive(); });
   return ok ? 1 : 0;
}

/* dir: 0 = no data, 1 = data from the drive, 2 = data to the drive (as CDROM_CMD_Direction).
 * Returns 0 when the command went through, 1 when not (the convention of cdrom_send_command_*). */
extern "C" int uwp_cdrom_command(int dir, void *buf, size_t len,
      unsigned char *cmd, size_t cmd_len, unsigned char *sense, size_t sense_len)
{
   int rv = 1;
   if (len > MAX_DATA || cmd_len > 16)
      return 1;
   /* SET CD SPEED: the console's drive refuses it (it picks its own speed), and cdrom.c would try it again
    * ten times, a second apart, on every open. */
   if (cmd[0] == 0xBB)
      return 0;
   /* READ CD MSF (addresses as minute, second, frame) is refused too, READ CD (sector numbers) is not:
    * the one is rewritten as the other. Sector number = (minute * 60 + second) * 75 + frame - 150. */
   unsigned char read_cd[12];
   if (cmd[0] == 0xB9 && cmd_len == 12)
   {
      const int start = (cmd[3] * 60 + cmd[4]) * 75 + cmd[5] - 150;
      const int end   = (cmd[6] * 60 + cmd[7]) * 75 + cmd[8] - 150;
      const int count = end - start;
      if (start < 0 || count <= 0)
         return 1;
      read_cd[0]  = 0xBE;
      read_cd[1]  = cmd[1];
      read_cd[2]  = (unsigned char)(start >> 24);
      read_cd[3]  = (unsigned char)(start >> 16);
      read_cd[4]  = (unsigned char)(start >> 8);
      read_cd[5]  = (unsigned char)start;
      read_cd[6]  = (unsigned char)(count >> 16);
      read_cd[7]  = (unsigned char)(count >> 8);
      read_cd[8]  = (unsigned char)count;
      read_cd[9]  = cmd[9];
      read_cd[10] = cmd[10];
      read_cd[11] = cmd[11];
      cmd         = read_cd;
   }

   run_on_worker([&] {
      if (!open_drive())
         return;

      SCSI_PASS_THROUGH spt{};
      spt.Length             = sizeof(SCSI_PASS_THROUGH);
      spt.CdbLength          = (UCHAR)cmd_len;
      spt.SenseInfoLength    = 32;
      spt.DataIn             = dir == 1 ? SCSI_IOCTL_DATA_IN : dir == 2 ? SCSI_IOCTL_DATA_OUT : SCSI_IOCTL_DATA_UNSPECIFIED;
      spt.DataTransferLength = dir == 0 ? 0 : (ULONG)len;
      spt.TimeOutValue       = 20;
      spt.DataBufferOffset   = SPT_HEADER;
      spt.SenseInfoOffset    = sizeof(SCSI_PASS_THROUGH);
      memcpy(spt.Cdb, cmd, cmd_len);

      uint8_t *p = g_drive->in.data();
      memset(p, 0, SPT_HEADER);
      memcpy(p, &spt, sizeof(spt));
      if (dir == 2 && buf)
         memcpy(p + SPT_HEADER, buf, len);
      g_drive->in.Length(SPT_HEADER + (dir == 2 ? (uint32_t)len : (dir == 1 ? (uint32_t)len : 0)));

      try
      {
         const uint32_t n = g_drive->device.SendIOControlAsync(
            IOControlCode(0x04 /* IOCTL_SCSI_BASE */, 0x401, IOControlAccessMode::ReadWrite, IOControlBufferingMethod::Buffered),
            g_drive->in, g_drive->out).get();
         if (n < sizeof(SCSI_PASS_THROUGH))
            return;
         const uint8_t *q = g_drive->out.data();
         memcpy(&spt, q, sizeof(spt));
         if (sense && sense_len)
         {
            memset(sense, 0, sense_len);
            if (n >= SPT_HEADER)
               memcpy(sense, q + sizeof(SCSI_PASS_THROUGH), std::min<size_t>(sense_len, 32));
         }
         if (spt.ScsiStatus != 0)
         {
            const uint8_t *sn = q + sizeof(SCSI_PASS_THROUGH);
            if (cmd[0] == 0x00)   /* TEST UNIT READY from the disc watch: "no disc" is its normal answer */
               return;
            drive_log("command %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x, %u bytes: status %02x, sense %x/%02x/%02x",
               cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], (unsigned)len,
               spt.ScsiStatus, sn[2] & 15, sn[12], sn[13]);
            return;
         }
         drive_log("command %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x, %u bytes: ok, %u came back",
            cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], (unsigned)len,
            (unsigned)spt.DataTransferLength);
         if (dir == 1 && buf)
         {
            const uint32_t moved = std::min<uint32_t>({(uint32_t)spt.DataTransferLength, (uint32_t)len,
               n > SPT_HEADER ? n - SPT_HEADER : 0u});
            memcpy(buf, q + SPT_HEADER, moved);
         }
         rv = 0;
      }
      catch (winrt::hresult_error const&)
      {
         /* the drive went away (console resumed, disc tray): open it again next time */
         g_drive->device = nullptr;
      }
   });
   return rv;
}

/* Game Vault: changing discs. While a game runs from the drive, a thread asks the drive once a second whether a
 * disc is in it (TEST UNIT READY). Taking the disc out, or a different disc becoming ready, is handed to the main
 * loop as an event (uwp_cdrom_media_event), which opens and closes the core's virtual disc lid as a player would
 * have done in Quick Menu > Disc Control. */
namespace
{
   std::atomic<bool> g_watch{false};
   std::atomic<int>  g_media_event{0};   /* 0 nothing, 1 the disc went, 2 a disc is ready */
   bool              g_watch_thread = false;

   void watch_loop()
   {
      bool present = true;   /* the game was started from a disc that was there */
      int  ready_in_a_row = 0;
      int  last_answer    = -1;
      int  not_ready_in_a_row = 0;
      drive_log("disc watch: thread started");
      for (;;)
      {
         std::this_thread::sleep_for(std::chrono::milliseconds(1000));
         if (!g_watch)
         {
            present        = true;
            ready_in_a_row = 0;
            not_ready_in_a_row = 0;
            continue;
         }
         unsigned char cmd[6] = {0x00, 0, 0, 0, 0, 0}, sense[32] = {0};
         const int not_ready = uwp_cdrom_command(0, nullptr, 0, cmd, sizeof(cmd), sense, sizeof(sense));
         const int asc       = sense[12];
         /* each change of the drive's answer, for looking into a swap that is not noticed */
         if ((not_ready ? asc : 0x100) != last_answer)
         {
            last_answer = not_ready ? asc : 0x100;
            drive_log("disc watch: drive says %s (sense %02x, code %d)", not_ready ? "not ready" : "ready", asc, not_ready);
         }
         if (!not_ready)
         {
            not_ready_in_a_row = 0;
            /* ready twice in a row: the drive has finished spinning up and reading the new disc */
            if (!present && ++ready_in_a_row >= 2)
            {
               present = true;
               drive_log("disc watch: a disc is ready");
               g_media_event = 2;
            }
            continue;
         }
         ready_in_a_row = 0;
         /* 3A: no disc. Other answers (28 "the disc changed", 04 "spinning up") also come right after the game
          * starts, so one of them alone is not taken as a swap. The Series X drive gives no sense data at all
          * (sense 00 for 14 s while the user swapped discs, 2026-10-17): "not ready" for 3 checks in a row is a
          * swap too. */
         not_ready_in_a_row++;
         if (present && (asc == 0x3A || not_ready_in_a_row >= 3))
         {
            present = false;
            drive_log("disc watch: the disc went (sense %02x, not ready %d s)", asc, not_ready_in_a_row);
            g_media_event = 1;
         }
      }
   }
}

extern "C" void uwp_cdrom_watch(int on)
{
   drive_log(on ? "disc watch: on (a game from the drive)" : "disc watch: off");
   if (on && !g_watch_thread)
   {
      g_watch_thread = true;
      std::thread(watch_loop).detach();
   }
   g_media_event = 0;
   g_watch       = on != 0;
}

extern "C" int uwp_cdrom_media_event(void)
{
   return g_media_event.exchange(0);
}
