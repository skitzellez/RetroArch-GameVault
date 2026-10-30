/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like RetroArch.
 *
 * The player's own touch buttons, laid out in Game Vault for Android ("Edit touch controls"): Game Vault sends the
 * layout for the game's system in the CONTROLS extra, and this turns it into a RetroArch overlay - one for the phone
 * held sideways, one held upright, RetroArch switching between them as the phone turns. The button pictures are
 * RetroArch's own (the neo-retropad overlay, CC-BY 4.0, inside this app). Without the extra, the overlay the player
 * had before (RetroArch's own, or one picked in its menu) comes back.
 *
 * CONTROLS: one button per line, "<landscape|portrait> <button> <x> <y> <size>": the button's centre as a part of
 * the screen's width and height (0-1), its width as a part of the screen's short side. Other apps can open this
 * door too, so every line is checked and the button names come from the list below only.
 */
package com.retroarch.browser.gamevault;

import android.app.Activity;
import android.util.DisplayMetrics;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

final class TouchControls
{
   private static final String TAG = "GameVault";
   private static final String IMG = "overlays/gamepads/neo-retropad/img/default/";

   /* button -> RetroArch's name for it, its picture */
   private static final Map<String, String[]> BUTTONS = new HashMap<>();
   static
   {
      String[][] b = {
         { "a", "a", "button_a" }, { "b", "b", "button_b" }, { "x", "x", "button_x" }, { "y", "y", "button_y" },
         { "l", "l", "button_l1" }, { "r", "r", "button_r1" }, { "l2", "l2", "button_l2" }, { "r2", "r2", "button_r2" },
         { "l3", "l3", "button_l3" }, { "r3", "r3", "button_r3" },
         { "start", "start", "button_start" }, { "select", "select", "button_select" },
         { "menu", "menu_toggle", "hotkey_menu" }, { "ffwd", "toggle_fast_forward", "hotkey_fast_forward" },
         { "dpad", null, null }, { "lstick", "analog_left", null }, { "rstick", "analog_right", null },
      };
      for (String[] e : b)
         BUTTONS.put(e[0], new String[] { e[1], e[2] });
   }

   private static final class Placed
   {
      String button;
      float x, y, size;
   }

   /* the layout from Game Vault -> the overlay file RetroArch loads; null when there is none or it is not valid */
   static File write(Activity a, String layout)
   {
      if (layout == null || layout.length() > 8192)
         return null;
      List<Placed> land = new ArrayList<>(), port = new ArrayList<>();
      for (String line : layout.split("\n"))
      {
         String[] f = line.trim().split(" ");
         if (f.length != 5 || !BUTTONS.containsKey(f[1]))
            continue;
         Placed p = new Placed();
         p.button = f[1];
         try
         {
            p.x    = Float.parseFloat(f[2]);
            p.y    = Float.parseFloat(f[3]);
            p.size = Float.parseFloat(f[4]);
         }
         catch (NumberFormatException e)
         {
            continue;
         }
         if (!(p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1 && p.size >= 0.03f && p.size <= 0.8f))
            continue;
         if ("landscape".equals(f[0]))
            land.add(p);
         else if ("portrait".equals(f[0]))
            port.add(p);
      }
      if (land.isEmpty() && port.isEmpty())
         return null;
      File dir = new File(a.getExternalFilesDir(null), "gv_controls");
      File img = new File(dir, "img");
      img.mkdirs();

      DisplayMetrics m = new DisplayMetrics();
      a.getWindowManager().getDefaultDisplay().getRealMetrics(m);
      float r = (float) Math.min(m.widthPixels, m.heightPixels) / Math.max(m.widthPixels, m.heightPixels);
      if (!(r > 0.2f && r <= 1f))
         r = 9f / 16f;

      StringBuilder cfg = new StringBuilder();
      cfg.append("overlays = 2\n");
      String[] names = { "landscape-gamevault", "portrait-gamevault" };
      /* the phone's own shape (width / height): without it RetroArch takes a 16:9 screen and squeezes the buttons
       * on a taller phone */
      float[] shape = { 1f / r, r };
      for (int i = 0; i < 2; i++)
         cfg.append(String.format(Locale.ROOT,
            "overlay%d_name = \"%s\"\noverlay%d_full_screen = true\noverlay%d_normalized = true\n"
            + "overlay%d_aspect_ratio = %.6f\noverlay%d_range_mod = 1.5\noverlay%d_alpha_mod = 2.0\n",
            i, names[i], i, i, i, shape[i], i, i));
      overlay(a, cfg, 0, land, r, 1f, names[1], img);
      overlay(a, cfg, 1, port, 1f, r, names[0], img);

      File out = new File(dir, "controls.cfg");
      try (OutputStream o = new FileOutputStream(out))
      {
         o.write(cfg.toString().getBytes("UTF-8"));
      }
      catch (IOException e)
      {
         Log.w(TAG, "touch controls: " + e);
         return null;
      }
      Log.i(TAG, "touch controls: " + land.size() + " buttons sideways, " + port.size() + " upright");
      return out;
   }

   /* ux, uy: a part of the short side, as a part of the width and of the height */
   private static void overlay(Activity a, StringBuilder cfg, int n, List<Placed> list, float ux, float uy,
         String other, File img)
   {
      List<String> descs = new ArrayList<>();
      for (Placed p : list)
      {
         String[] b = BUTTONS.get(p.button);
         float w = p.size * ux / 2f, h = p.size * uy / 2f;   /* half the width and height of a square of that size */
         if ("dpad".equals(p.button))
         {
            /* the four arrows round the centre, and the four corners between them for diagonals (neo-retropad's) */
            String[][] arrows = { { "up", "0", "-1" }, { "down", "0", "1" }, { "left", "-1", "0" }, { "right", "1", "0" } };
            for (String[] ar : arrows)
            {
               int dx = Integer.parseInt(ar[1]), dy = Integer.parseInt(ar[2]);
               float hw = dx == 0 ? 0.385f : 0.475f, hh = dx == 0 ? 0.475f : 0.385f;
               descs.add(desc(ar[0], p.x + dx * 0.547f * w, p.y + dy * 0.547f * h, "rect", hw * w, hh * h)
                  + pic(a, img, "dpad_" + ar[0]));
            }
            String[][] corners = { { "up|left", "-1", "-1" }, { "up|right", "1", "-1" }, { "down|left", "-1", "1" },
               { "down|right", "1", "1" } };
            for (String[] c : corners)
               descs.add(desc(c[0], p.x + Integer.parseInt(c[1]) * 0.75f * w, p.y + Integer.parseInt(c[2]) * 0.75f * h,
                  "rect", 0.27f * w, 0.27f * h));
         }
         else if (p.button.endsWith("stick"))
         {
            /* the ring, and the stick inside it that follows the thumb */
            descs.add(desc(b[0], p.x, p.y, "radial", w, h) + pic(a, img, "analog_bg") + "#saturate_pct = 1000000.0\n");
            descs.add(desc(b[0], p.x, p.y, "radial", 0.58f * w, 0.58f * h) + pic(a, img, "analog_stick")
               + "#movable = true\n#range_mod = 4.0\n#range_mod_exclusive = true\n");
         }
         else
         {
            float aspect = aspect(a, b[1]);
            descs.add(desc(b[0], p.x, p.y, "rect", w, h * aspect) + pic(a, img, b[1]));
         }
      }
      /* turning the phone goes to the other overlay (RetroArch looks for a button that leads there) */
      descs.add(desc("overlay_next", 0.5f, 0.001f, "radial", 0.001f, 0.001f) + "#next_target = \"" + other + "\"\n");

      cfg.append(String.format(Locale.ROOT, "overlay%d_descs = %d\n", n, descs.size()));
      for (int i = 0; i < descs.size(); i++)
      {
         String prefix = String.format(Locale.ROOT, "overlay%d_desc%d", n, i);
         String[] lines = descs.get(i).split("\n");
         cfg.append(prefix).append(" = ").append(lines[0]).append('\n');
         for (int j = 1; j < lines.length; j++)
            cfg.append(prefix).append('_').append(lines[j].substring(1)).append('\n');
      }
   }

   private static String desc(String key, float x, float y, String shape, float rx, float ry)
   {
      return String.format(Locale.ROOT, "\"%s,%.5f,%.5f,%s,%.5f,%.5f\"\n", key, x, y, shape, rx, ry);
   }

   /* "#overlay = ..." for the picture, copied out of the app the first time */
   private static String pic(Activity a, File img, String name)
   {
      File f = new File(img, name + ".png");
      if (!f.isFile())
      {
         try (InputStream in = a.getAssets().open(IMG + name + ".png"); OutputStream o = new FileOutputStream(f))
         {
            byte[] buf = new byte[16384];
            for (int n; (n = in.read(buf)) > 0; )
               o.write(buf, 0, n);
         }
         catch (IOException e)
         {
            f.delete();
            Log.w(TAG, "touch controls picture " + name + ": " + e);
            return "";
         }
      }
      return "#overlay = \"img/" + name + ".png\"\n";
   }

   /* height / width of a picture, from its PNG header (the shoulder buttons are wider than tall) */
   private static float aspect(Activity a, String name)
   {
      try (InputStream in = a.getAssets().open(IMG + name + ".png"))
      {
         byte[] h = new byte[24];
         if (in.read(h) == 24)
         {
            int w  = ((h[16] & 0xff) << 24) | ((h[17] & 0xff) << 16) | ((h[18] & 0xff) << 8) | (h[19] & 0xff);
            int ht = ((h[20] & 0xff) << 24) | ((h[21] & 0xff) << 16) | ((h[22] & 0xff) << 8) | (h[23] & 0xff);
            if (w > 0 && ht > 0)
               return (float) ht / w;
         }
      }
      catch (IOException e)
      {
         /* square */
      }
      return 1f;
   }
}
