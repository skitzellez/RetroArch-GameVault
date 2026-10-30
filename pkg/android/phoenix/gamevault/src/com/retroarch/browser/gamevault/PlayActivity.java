/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like RetroArch.
 *
 * The door Game Vault for Android opens to start a game: extras ROM (the game file) and CORE (the core's name,
 * e.g. "snes9x"). Nothing to set up for the player: the cores come inside the app (lib/arm64-v8a, linked into
 * RetroArch's own cores folder here), the system files some cores need come inside too (res/raw, unpacked into the
 * BIOS folder the first time that core plays), a first settings file is written once, and the only question is
 * Android's "All files access", asked here in plain words before RetroArch starts.
 */
package com.retroarch.browser.gamevault;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.system.Os;
import android.util.Log;
import android.widget.Toast;

import com.retroarch.browser.retroactivity.RetroActivityFuture;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class PlayActivity extends Activity
{
   private static final String TAG = "GameVault";
   private static final int REQUEST_ALL_FILES = 1;

   /* Written once; the player's own changes in RetroArch's menu are kept after that. */
   private static final String FIRST_CONFIG =
        "input_menu_toggle_gamepad_combo = \"7\"\n"            /* hold START: menu (pads without L3/R3 too) */
      + "input_overlay_enable = \"true\"\n"                    /* touch buttons on a phone ... */
      + "input_overlay_hide_when_gamepad_connected = \"true\"\n" /* ... gone when a controller is in */
      + "quit_on_close_content = \"2\"\n"                      /* closing the game goes back to Game Vault */
      + "pause_nonactive = \"true\"\n"
      + "savestate_thumbnail_enable = \"true\"\n"
      + "system_directory = \"" + biosDir().getPath() + "\"\n";

   /* BIOS files: Game Vault puts any it finds next to the games here (free replacements are built into the
    * cores where they exist). */
   static File biosDir()
   {
      return new File(Environment.getExternalStorageDirectory(), "GameVault/bios");
   }

   @Override
   public void onCreate(Bundle savedInstanceState)
   {
      super.onCreate(savedInstanceState);
      if (hasStorage())
      {
         start();
         return;
      }
      new AlertDialog.Builder(this)
         .setTitle("One step before the first game")
         .setMessage("Android asks once: on the next screen, turn on \"Allow access to manage all files\" so "
            + "your games can be opened, then come back.")
         .setPositiveButton(android.R.string.ok, (d, w) -> askStorage())
         .setOnCancelListener(d -> finish())
         .show();
   }

   private boolean hasStorage()
   {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
         return Environment.isExternalStorageManager();
      return checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE)
         == android.content.pm.PackageManager.PERMISSION_GRANTED;
   }

   private void askStorage()
   {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
      {
         try
         {
            startActivityForResult(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
               Uri.parse("package:" + getPackageName())), REQUEST_ALL_FILES);
         }
         catch (Exception e)
         {
            /* some TV boxes have no page per app */
            startActivityForResult(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), REQUEST_ALL_FILES);
         }
         return;
      }
      requestPermissions(new String[] { android.Manifest.permission.READ_EXTERNAL_STORAGE,
         android.Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQUEST_ALL_FILES);
   }

   @Override
   public void onActivityResult(int requestCode, int resultCode, Intent data)
   {
      if (requestCode == REQUEST_ALL_FILES)
         resolved();
   }

   @Override
   public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults)
   {
      if (requestCode == REQUEST_ALL_FILES)
         resolved();
   }

   private void resolved()
   {
      if (hasStorage())
         start();
      else
      {
         Toast.makeText(this, "Games cannot be opened without that access.", Toast.LENGTH_LONG).show();
         finish();
      }
   }

   private void start()
   {
      Intent in   = getIntent();
      String rom  = in.getStringExtra("ROM");
      String core = in.getStringExtra("CORE");
      /* a core's plain name only: other apps can open this door too, and "../" would load code from elsewhere */
      if (core != null && !core.matches("[a-z0-9_]{1,64}"))
         core = null;
      File cores  = linkCores();
      File cfg    = firstConfig();
      systemFiles(core);
      if (cfg != null)
      {
         /* Continue anywhere: closing a game always writes a save state of where it was (states/<core>/<game>
          * .state.auto); Game Vault puts the one from the Xbox there and sends AUTOLOAD=1 to go on from it.
          * Any other start begins the game the usual way. */
         java.util.Map<String, String> set = new java.util.LinkedHashMap<>();
         set.put("savestate_auto_save", "true");
         set.put("savestate_auto_load", "1".equals(in.getStringExtra("AUTOLOAD")) ? "true" : "false");
         setKeys(cfg, set);
      }
      if (cfg != null)
         touchControls(cfg, in.getStringExtra("CONTROLS"));
      if ("1".equals(in.getStringExtra("NETPLAY_HOST")) && cfg != null)
         hostThroughRelay(cfg, in.getStringExtra("NETPLAY_RELAY"));

      Intent i = new Intent(this, RetroActivityFuture.class);
      if (in.getExtras() != null)
         i.putExtras(in.getExtras());       /* REFRESH, QUITFOCUS ... pass through */
      i.removeExtra("CORE");
      if (core != null)
      {
         /* the core itself in the app's native folder; the link in cores/ only where that is missing */
         File lib = new File(getApplicationInfo().nativeLibraryDir, "lib" + core + "_libretro_android.so");
         i.putExtra("LIBRETRO", (lib.isFile() ? lib : new File(cores, core + "_libretro_android.so")).getPath());
      }
      if (rom != null)
         i.putExtra("ROM", rom);
      if (cfg != null)
         i.putExtra("CONFIGFILE", cfg.getPath());
      i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
      Log.i(TAG, "play core=" + core + " rom=" + rom);
      startActivity(i);
      finish();
   }

   /* lib<name>_libretro_android.so from the app's native folder -> <data>/cores/<name>_libretro_android.so,
    * RetroArch's own cores folder, so its menu (Load Core) lists them too. A link costs nothing; games
    * never depend on these links (start() passes the core's own path):
    * RetroActivityCommon.cleanupSymlinks() used to delete them at every start, which put every game on a black
    * screen. The names come from the app's own APK. */
   private File linkCores()
   {
      File cores = new File(getApplicationInfo().dataDir, "cores");
      cores.mkdirs();
      String dir = getApplicationInfo().nativeLibraryDir;
      java.util.Set<String> names = coreLibs();
      int linked = 0, missing = 0;
      for (String n : names)
      {
         File lib = new File(dir, n);
         if (!lib.isFile())
         {
            missing++;
            continue;
         }
         File dst = new File(cores, n.substring(3));
         try
         {
            if (lib.getPath().equals(Os.readlink(dst.getPath())))
               continue;
         }
         catch (Exception e)
         {
            /* not a link (yet) */
         }
         dst.delete();
         try
         {
            Os.symlink(lib.getPath(), dst.getPath());
            linked++;
         }
         catch (Exception e)
         {
            /* RetroArch's Load Core menu misses this core; games still play */
            if (missing++ == 0)
               Log.w(TAG, "link " + n + ": " + e);
         }
      }
      Log.i(TAG, "cores: " + names.size() + " in the app, " + linked + " linked, " + missing + " not linked");
      return cores;
   }

   /* lib<name>_libretro_android.so file names inside the APK (lib/<abi>/), read from its table of contents */
   private java.util.Set<String> coreLibs()
   {
      java.util.Set<String> out = new java.util.TreeSet<>();
      try (java.util.zip.ZipFile apk = new java.util.zip.ZipFile(getApplicationInfo().sourceDir))
      {
         java.util.Enumeration<? extends ZipEntry> e = apk.entries();
         while (e.hasMoreElements())
         {
            String n = e.nextElement().getName();
            if (!n.startsWith("lib/"))
               continue;
            n = n.substring(n.lastIndexOf('/') + 1);
            if (n.startsWith("lib") && n.endsWith("_libretro_android.so"))
               out.add(n);
         }
      }
      catch (IOException e)
      {
         Log.w(TAG, "core list: " + e);
      }
      return out;
   }

   /* A hosted room goes through one of RetroArch's relay servers (a phone cannot open a port on the router) and is
    * listed in the public lobby, so Xbox, PC and other phones see it. relay: the server's short name ("nyc"). */
   private static void hostThroughRelay(File cfg, String relay)
   {
      if (relay == null || !relay.matches("[a-z]{1,32}"))
         relay = "nyc";
      java.util.Map<String, String> set = new java.util.LinkedHashMap<>();
      set.put("netplay_use_mitm_server", "true");
      set.put("netplay_mitm_server", relay);
      set.put("netplay_public_announce", "true");
      if (setKeys(cfg, set))
         Log.i(TAG, "hosting through the " + relay + " relay");
   }

   /* The player's own touch buttons from Game Vault (TouchControls), or, without them, the overlay they had before.
    * What was there before ours is kept in gv_controls/previous.txt. */
   private void touchControls(File cfg, String layout)
   {
      File dir      = new File(getExternalFilesDir(null), "gv_controls");
      File previous = new File(dir, "previous.txt");
      String now    = getKey(cfg, "input_overlay");
      boolean ours  = now != null && now.contains("/gv_controls/");
      File made     = TouchControls.write(this, layout);
      java.util.Map<String, String> set = new java.util.LinkedHashMap<>();
      if (made != null)
      {
         if (!ours)
            try (OutputStream o = new FileOutputStream(previous))
            {
               o.write((now == null ? "" : now).getBytes("UTF-8"));
            }
            catch (IOException e)
            {
               Log.w(TAG, "touch controls: " + e);
            }
         set.put("input_overlay", made.getPath());
         set.put("input_overlay_enable", "true");
         set.put("input_overlay_auto_rotate", "true");
      }
      else if (ours)
      {
         String back = "";
         try
         {
            back = new String(java.nio.file.Files.readAllBytes(previous.toPath()), "UTF-8").trim();
         }
         catch (IOException e)
         {
            /* RetroArch's own overlay ("" = its default) */
         }
         set.put("input_overlay", back);
      }
      if (!set.isEmpty())
         setKeys(cfg, set);
   }

   /* a setting's value from the settings file, or null */
   private static String getKey(File cfg, String key)
   {
      try
      {
         for (String line : new String(java.nio.file.Files.readAllBytes(cfg.toPath()), "UTF-8").split("\n"))
         {
            String[] kv = line.split("=", 2);
            if (kv.length == 2 && kv[0].trim().equals(key))
               return kv[1].trim().replaceAll("^\"|\"$", "");
         }
      }
      catch (IOException e)
      {
         /* no file yet */
      }
      return null;
   }

   /* the given settings into the settings file, the rest of it as it was */
   private static boolean setKeys(File cfg, java.util.Map<String, String> set)
   {
      StringBuilder out = new StringBuilder();
      try
      {
         for (String line : new String(java.nio.file.Files.readAllBytes(cfg.toPath()), "UTF-8").split("\n"))
         {
            String key = line.split("=", 2)[0].trim();
            if (!set.containsKey(key))
               out.append(line).append('\n');
         }
      }
      catch (IOException e)
      {
         /* a new file */
      }
      for (java.util.Map.Entry<String, String> e : set.entrySet())
         out.append(e.getKey()).append(" = \"").append(e.getValue()).append("\"\n");
      try (OutputStream o = new FileOutputStream(cfg))
      {
         o.write(out.toString().getBytes("UTF-8"));
      }
      catch (IOException e)
      {
         Log.w(TAG, "settings file: " + e);
         return false;
      }
      return true;
   }

   private File firstConfig()
   {
      File dir = getExternalFilesDir(null);
      if (dir == null)
         return null;
      File cfg = new File(dir, "retroarch.cfg");
      biosDir().mkdirs();
      if (cfg.exists())
         return cfg;
      try (OutputStream o = new FileOutputStream(cfg))
      {
         o.write(FIRST_CONFIG.getBytes("UTF-8"));
      }
      catch (IOException e)
      {
         Log.w(TAG, "settings file: " + e);
      }
      return cfg;
   }

   /* core -> its system files (libretro's packs, unpacked as they are into the BIOS folder) and the folder that
    * shows they are there */
   private static final String[][] SYSTEM_FILES = {
      { "dolphin",  "gv_sys_dolphin", "dolphin-emu/Sys" },
      { "ppsspp",   "gv_sys_ppsspp",  "PPSSPP" },
      { "armsx2",   "gv_sys_armsx2",  "pcsx2/armsx2-pack" },
      { "bluemsx",  "gv_sys_bluemsx", "Machines" },
   };

   /* The free PS2 BIOS (OpenPS2BIOS) steps aside while the player has a BIOS of their own in pcsx2/bios (ARMSX2
    * takes the first valid one it finds, in no set order; it does not look into sub-folders) and comes back when
    * theirs is gone. */
   private void freePs2Bios()
   {
      File bios  = new File(biosDir(), "pcsx2/bios");
      File aside = new File(bios, "free");
      String name = "OpenPS2BIOS-0.2.bin";
      boolean own = false;
      File[] fs = bios.listFiles();
      if (fs != null)
         for (File f : fs)
            if (f.isFile() && !f.getName().equals(name) && f.length() >= 4L * 1024 * 1024 && f.length() <= 8L * 1024 * 1024)
               own = true;
      File here = new File(bios, name), away = new File(aside, name);
      if (own && here.isFile())
      {
         aside.mkdirs();
         if (here.renameTo(away))
            Log.i(TAG, "PS2: the player's own BIOS is used, the free one is set aside");
      }
      else if (!own && !here.isFile() && away.isFile() && away.renameTo(here))
         Log.i(TAG, "PS2: no BIOS of the player's own, the free one is back");
   }

   private void systemFiles(String core)
   {
      if (core.equals("armsx2"))
         freePs2Bios();
      for (String[] f : SYSTEM_FILES)
      {
         if (!f[0].equals(core) || new File(biosDir(), f[2]).isDirectory())
            continue;
         int id = getResources().getIdentifier(f[1], "raw", getPackageName());
         if (id == 0)
            continue;
         try (InputStream in = getResources().openRawResource(id))
         {
            unzip(in, biosDir());
            Log.i(TAG, "system files for " + core + " unpacked");
            if (core.equals("armsx2"))
               freePs2Bios();
         }
         catch (IOException e)
         {
            Log.w(TAG, "system files for " + core + ": " + e);
         }
      }
   }

   private static void unzip(InputStream in, File to) throws IOException
   {
      String root = to.getCanonicalPath() + File.separator;
      byte[] b    = new byte[1 << 16];
      try (ZipInputStream z = new ZipInputStream(in))
      {
         ZipEntry e;
         while ((e = z.getNextEntry()) != null)
         {
            File out = new File(to, e.getName());
            /* nothing outside the BIOS folder ("../" names) */
            if (!out.getCanonicalPath().startsWith(root))
               continue;
            if (e.isDirectory())
            {
               out.mkdirs();
               continue;
            }
            out.getParentFile().mkdirs();
            try (OutputStream o = new FileOutputStream(out))
            {
               int n;
               while ((n = z.read(b)) > 0)
                  o.write(b, 0, n);
            }
         }
      }
   }
}
