/* MegaZeux
 *
 * Copyright (C) 1996 Greg Janson
 * Copyright (C) 1999 Charles Goetzman
 * Copyright (C) 2004 Gilead Kutnick <exophase@adelphia.net>
 * Copyright (C) 2018 Alice Rowan <petrifiedrowan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// Main title screen/gaming code

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "caption.h"
#include "configure.h"
#include "const.h"
#include "core.h"
#include "counter.h"
#include "data.h"
#include "error.h"
#include "event.h"
#include "fsafeopen.h"
#include "game.h"
#include "game_menu.h"
#include "game_player.h"
#include "game_update.h"
#include "graphics.h"
#include "platform.h"
#include "robot.h"
#include "util.h"
#include "window.h"
#include "world.h"
#include "world_struct.h"

#include "audio/audio.h"
#include "audio/sfx.h"

static const char *const world_ext[] = { ".MZX", NULL };
static const char *const save_ext[] = { ".SAV", NULL };

struct game_context
{
  context ctx;
  boolean fade_in;
  boolean need_reload;
  boolean load_dialog_on_failed_load;
  boolean is_title;
};

/**
 * Load a module for gameplay.
 */

boolean load_game_module(struct world *mzx_world, char *filename,
 boolean fail_if_same)
{
  struct board *cur_board = mzx_world->current_board;
  char translated_name[MAX_PATH];
  size_t mod_name_size = strlen(filename);
  boolean mod_star = false;
  int n_result;

  // Special case: mod "" ends the module.
  if(!filename[0])
  {
    mzx_world->real_mod_playing[0] = 0;
    audio_end_module();
    return false;
  }

  // Do nothing.
  if(!strcmp(filename, "*"))
    return false;

  // Temporarily get rid of the star.
  if(mod_name_size && filename[mod_name_size - 1] == '*')
  {
    filename[mod_name_size - 1] = 0;
    mod_star = true;
  }

  // Get the translated name (the one we want to compare against later)
  n_result = fsafetranslate(filename, translated_name);

  // Add * back
  if(mod_star)
    filename[mod_name_size - 1] = '*';

  if(n_result == FSAFE_SUCCESS)
  {
    // In certain situations, play the mod only if the names are different.
    if((mod_star || fail_if_same) &&
     !strcasecmp(translated_name, mzx_world->real_mod_playing))
      return false;

    // If the mod actually changes, update real mod playing.
    // The safety check has already been done, so don't do it again ('false').
    if(audio_play_module(translated_name, false, cur_board->volume))
    {
      strcpy(mzx_world->real_mod_playing, translated_name);
      return true;
    }
  }
  return false;
}

/**
 * Load the current board's module, even if it's already playing.
 */

void load_board_module(struct world *mzx_world)
{
  load_game_module(mzx_world, mzx_world->current_board->mod_playing, false);
}

/**
 * Load a world and prepare it for gameplay.
 * On failure, make sure the title's fade setting is restored (if applicable).
 */

static boolean load_world_gameplay(struct game_context *game, char *name)
{
  struct world *mzx_world = ((context *)game)->world;
  boolean was_faded = get_fade_status();
  boolean ignore;

  struct board *cur_board;

  // Save the name of the current playing mod; we'll want it to continue playing
  // into gameplay if the first board has the same mod or mod *.
  char old_mod_playing[MAX_PATH];
  strcpy(old_mod_playing, mzx_world->real_mod_playing);

  vquick_fadeout();
  clear_screen();

  game->fade_in = true;

  if(reload_world(mzx_world, name, &ignore))
  {
    if(mzx_world->current_board_id != mzx_world->first_board)
    {
      change_board(mzx_world, mzx_world->first_board);
    }

    cur_board = mzx_world->current_board;

    // Send both JUSTENTERED and JUSTLOADED; the JUSTLOADED label will
    // take priority if a robot defines it.
    send_robot_def(mzx_world, 0, LABEL_JUSTENTERED);
    send_robot_def(mzx_world, 0, LABEL_JUSTLOADED);

    change_board_set_values(mzx_world);
    change_board_load_assets(mzx_world);

    // Load the mod unless it's the mod from the title screen or *.
    strcpy(mzx_world->real_mod_playing, old_mod_playing);
    load_game_module(mzx_world, cur_board->mod_playing, true);
    sfx_clear_queue();

    caption_set_world(mzx_world);
    return true;
  }

  if(was_faded)
    game->fade_in = false;

  return false;
}

/**
 * Load a world and prepare it for the title screen.
 * On failure, display a blank screen.
 */

static boolean load_world_title(struct game_context *game, char *name)
{
  struct world *mzx_world = ((context *)game)->world;
  boolean ignore;

  vquick_fadeout();
  clear_screen();
  enable_intro_mesg();
  game->fade_in = true;

  if(reload_world(mzx_world, name, &ignore))
  {
    // Load was successful, so set curr_file
    if(curr_file != name)
      strcpy(curr_file, name);

    // Only send JUSTLOADED on the title screen.
    send_robot_def(mzx_world, 0, LABEL_JUSTLOADED);

    change_board_set_values(mzx_world);
    change_board_load_assets(mzx_world);

    load_board_module(mzx_world);
    sfx_clear_queue();

    caption_set_world(mzx_world);
    return true;
  }
  else

  if(mzx_world->active)
  {
    clear_world(mzx_world);
    clear_global_data(mzx_world);
  }

  return false;
}

/**
 * Load a saved game and prepare it for gameplay.
 * On failure, make sure the title world's fade setting is restored.
 */

static boolean load_savegame(struct game_context *game, char *name)
{
  struct world *mzx_world = ((context *)game)->world;
  boolean was_faded = get_fade_status();
  boolean save_is_faded;

  vquick_fadeout();
  clear_screen();
  game->fade_in = true;

  if(reload_savegame(mzx_world, name, &save_is_faded))
  {
    if(curr_sav != name)
      strcpy(curr_sav, name);

    // Only send JUSTLOADED for savegames.
    send_robot_def(mzx_world, 0, LABEL_JUSTLOADED);
    find_player(mzx_world);

    load_game_module(mzx_world, mzx_world->real_mod_playing, false);
    sfx_clear_queue();

    if(save_is_faded)
      game->fade_in = false;

    caption_set_world(mzx_world);
    return true;
  }

  if(was_faded)
    game->fade_in = false;

  return false;
}

/**
 * Open a user interface to select a world to load on the title.
 */

static boolean load_world_title_selection(struct game_context *game)
{
  struct world *mzx_world = ((context *)game)->world;
  char world_name[MAX_PATH] = { 0 };

  m_show();
  if(!choose_file_ch(mzx_world, world_ext, world_name, "Load World", true))
  {
    return load_world_title(game, world_name);
  }

  return false;
}

/**
 * Open a user interface to select a save to load.
 */

static boolean load_savegame_selection(struct game_context *game)
{
  struct world *mzx_world = ((context *)game)->world;
  char save_file_name[MAX_PATH] = { 0 };

  m_show();
  if(!choose_file_ch(mzx_world, save_ext, save_file_name,
    "Choose game to restore", true))
  {
    return load_savegame(game, save_file_name);
  }

  return false;
}

/**
 * Draw function for the title and gameplay. This actually includes most of the
 * game update cycle, including player input and updating the board.
 */

static void game_draw(context *ctx)
{
  struct game_context *game = (struct game_context *)ctx;
  struct config_info *conf = get_config(ctx);
  struct world *mzx_world = ctx->world;

  // No game state change has happened (yet)
  mzx_world->change_game_state = CHANGE_STATE_NONE;

  if(game->is_title && game->fade_in)
  {
    // Focus on center
    int x, y;
    set_screen_coords(640/2, 350/2, &x, &y);
    focus_pixel(x, y);
  }

  if(!mzx_world->active)
  {
    // There is no MZX_SPEED to derive a framerate from, so use the UI rate.
    set_context_framerate_mode(ctx, FRAMERATE_UI);
    if(!conf->standalone_mode)
      draw_intro_mesg(mzx_world);
    return;
  }

  set_context_framerate_mode(ctx, FRAMERATE_MZX_SPEED);
  update_world(ctx, game->is_title);
  draw_world(ctx, game->is_title);
}

// Forward declaration since this is used for both game and title screen.
__editor_maybe_static
void play_game(context *ctx, boolean *_fade_in);

/**
 * Idle function for the title and gameplay. Executes parts of the update
 * cycle that need to occur immediately after the delay.
 */

static boolean game_idle(context *ctx)
{
  struct game_context *game = (struct game_context *)ctx;
  struct config_info *conf = get_config(ctx);
  struct world *mzx_world = ctx->world;

  if(!mzx_world->active)
    return false;

  if(game->fade_in)
  {
    vquick_fadein();
    game->fade_in = false;
  }

  switch(mzx_world->change_game_state)
  {
    case CHANGE_STATE_NONE:
      break;

    case CHANGE_STATE_SWAP_WORLD:
    {
      // The SWAP WORLD command was used by a robot.
      // TODO: the game has already been loaded at this point, but maybe
      // should be loaded here instead of in run_robot.c?

      // Load the new board's mod
      load_board_module(mzx_world);

      // Send both JUSTLOADED and JUSTENTERED; the JUSTENTERED label will take
      // priority if a robot defines it (instead of JUSTLOADED like on the title
      // screen).
      send_robot_def(mzx_world, 0, LABEL_JUSTLOADED);
      send_robot_def(mzx_world, 0, LABEL_JUSTENTERED);

      return true;
    }

    case CHANGE_STATE_LOAD_GAME_ROBOTIC:
    {
      // The LOAD_GAME counter was used by a robot.
      // TODO: the game has already been loaded at this point, but maybe
      // should be loaded here instead of in counter.c?

      // real_mod_playing was set during the savegame load but the mod hasn't
      // started playing yet.
      load_game_module(mzx_world, mzx_world->real_mod_playing, false);

      // Only send JUSTLOADED for savegames.
      send_robot_def(mzx_world, 0, LABEL_JUSTLOADED);

      return true;
    }

    case CHANGE_STATE_PLAY_GAME_ROBOTIC:
    {
      if(!game->is_title)
        break;

      if(load_world_gameplay(game, curr_file))
      {
        play_game(ctx, NULL);
        game->need_reload = true;
        return true;
      }
      break;
    }

    case CHANGE_STATE_EXIT_GAME_ROBOTIC:
    {
      // The EXIT_GAME counter was used by a robot. This works during gameplay,
      // but also on the titlescreen if standalone mode is active.
      if(!game->is_title || conf->standalone_mode)
      {
        destroy_context(ctx);
        return true;
      }
      break;
    }

    case CHANGE_STATE_REQUEST_EXIT:
    {
      // The user halted the program while a robot was executing.
      destroy_context(ctx);
      return true;
    }
  }

  // A board change or other form of teleport may need to occur.
  // This may require a fade in the next time this function is run (next cycle).
  if(update_resolve_target(mzx_world, &(game->fade_in)))
    return true;

  // The SAVE_GAME counter might have been used this cycle.
  if(!game->is_title && mzx_world->robotic_save_type == SAVE_GAME)
  {
    save_world(mzx_world, mzx_world->robotic_save_path, true, MZX_VERSION);
    mzx_world->robotic_save_type = SAVE_NONE;
  }

  return false;
}

/**
 * Key function for the gameplay context. This handles interface keys and some
 * key labels; some other keys are handled in the update function.
 */

static boolean game_key(context *ctx, int *key)
{
  struct game_context *game = (struct game_context *)ctx;
  struct config_info *conf = get_config(ctx);
  struct world *mzx_world = ctx->world;
  struct board *cur_board = mzx_world->current_board;
  char keylbl[] = "KEY?";

  int key_status = get_key_status(keycode_internal_wrt_numlock, *key);
  boolean exit_status = get_exit_status();
  boolean confirm_exit = false;

  if(*key && !exit_status)
  {
#ifdef CONFIG_PANDORA
    // The Pandora (at least, at one point) did not have proper support for
    // keycode_unicode. This is a workaround to get the "keyN" labels to work
    // at all. This will break certain keyN labels, such as "key$", however.
    int key_char = *key;
#else
    int key_char = get_key(keycode_unicode);
#endif

    if(key_char)
    {
      keylbl[3] = key_char;
      send_robot_all_def(mzx_world, keylbl);
      // In pre-port MZX versions key was a board counter
      if(mzx_world->version < VERSION_PORT)
      {
        char keych = toupper(key_char);
        // <2.60 it only supported 1-9 and A-Z
        // This is difficult to version check, so apply it to <2.62
        if(mzx_world->version >= V262 ||
         (keych >= 'A' && keych <= 'Z') ||
         (keych >= '1' && keych <= '9'))
        {
          cur_board->last_key = keych;
        }
      }
    }

    switch(*key)
    {
      case IKEY_F3:
      {
        // Save game
        if(!mzx_world->dead && player_can_save(mzx_world))
        {
          char save_game[MAX_PATH];
          strcpy(save_game, curr_sav);

          m_show();
          if(!new_file(mzx_world, save_ext, ".sav", save_game, "Save game", 1))
          {
            strcpy(curr_sav, save_game);
            save_world(mzx_world, curr_sav, true, MZX_VERSION);
          }
        }
        return true;
      }

      case IKEY_F4:
      {
        // ALT+F4 - do nothing.
        if(get_alt_status(keycode_internal))
          break;

        // Restore saved game
        if(mzx_world->version < V282 || get_counter(mzx_world, "LOAD_MENU", 0))
        {
          load_savegame_selection(game);
        }
        return true;
      }

      case IKEY_F5:
      case IKEY_INSERT:
      {
        // Change bomb type
        if(!mzx_world->dead)
          player_switch_bomb_type(mzx_world);

        return true;
      }

      // Toggle debug mode
      case IKEY_F6:
      {
        if(edit_world && mzx_world->editing)
          mzx_world->debug_mode = !(mzx_world->debug_mode);
        return true;
      }

      // Cheat
      case IKEY_F7:
      {
        if(mzx_world->editing)
          player_cheat_give_all(mzx_world);

        return true;
      }

      // Cheat More
      case IKEY_F8:
      {
        if(mzx_world->editing)
          player_cheat_zap(mzx_world);

        return true;
      }

      // Quick save
      case IKEY_F9:
      {
        if(!mzx_world->dead)
        {
          if(player_can_save(mzx_world))
            save_world(mzx_world, curr_sav, true, MZX_VERSION);
        }
        return true;
      }

      // Quick load saved game
      case IKEY_F10:
      {
        if(mzx_world->version < V282 || get_counter(mzx_world, "LOAD_MENU", 0))
        {
          struct stat file_info;

          if(!stat(curr_sav, &file_info))
            load_savegame(game, curr_sav);
        }
        return true;
      }

      case IKEY_F11:
      {
        if(mzx_world->editing)
        {
          // Breakpoint editor
          if(get_alt_status(keycode_internal))
          {
            if(debug_robot_config)
              debug_robot_config(mzx_world);
          }
          // Counter debugger
          else
          {
            if(debug_counters)
              debug_counters(ctx);
          }
        }
        return true;
      }

      case IKEY_RETURN:
      {
        send_robot_all_def(mzx_world, "KeyEnter");

        // Ignore if this isn't a fresh press
        if(key_status != 1)
          return true;

        if(mzx_world->version < V260 || get_counter(mzx_world, "ENTER_MENU", 0))
          game_menu(ctx);

        return true;
      }

      case IKEY_ESCAPE:
      {
        // Ignore if this isn't a fresh press
        if(key_status != 1)
          return true;

        // ESCAPE_MENU (2.90+)
        if(mzx_world->version < V290 || get_counter(mzx_world, "ESCAPE_MENU", 0))
          confirm_exit = true;

        break;
      }
    }
  }

  // Quit
  if(exit_status || confirm_exit)
  {
    // Special behaviour in standalone- only escape exits
    // ask for confirmation. Exit events instead terminate MegaZeux.
    if(conf->standalone_mode && !confirm_exit)
    {
      core_exit(ctx);
    }
    else
    {
      m_show();

      if(!confirm(mzx_world, "Quit playing- Are you sure?"))
        destroy_context(ctx);
    }
    return true;
  }

  return false;
}

/**
 * Destroy a game context.
 */

static void game_destroy(context *ctx)
{
  vquick_fadeout();
  clear_screen();
  audio_end_module();
  sfx_clear_queue();
}

/**
 * Create and run the gameplay context.
 */

__editor_maybe_static
void play_game(context *parent, boolean *_fade_in)
{
  struct game_context *game;
  struct context_spec spec;

  game = cmalloc(sizeof(struct game_context));
  game->fade_in = _fade_in ? * _fade_in : true;
  game->is_title = false;

  // Not used for gameplay...
  game->need_reload = false;
  game->load_dialog_on_failed_load = false;

  memset(&spec, 0, sizeof(struct context_spec));
  spec.draw     = game_draw;
  spec.idle     = game_idle;
  spec.key      = game_key;
  spec.destroy  = game_destroy;

  spec.framerate_mode = FRAMERATE_MZX_SPEED;

  create_context((context *)game, parent, &spec, CTX_PLAY_GAME);

  if(!edit_world)
    parent->world->editing = false;

  clear_intro_mesg();
}

/**
 * When control returns to the title screen from gameplay or the editor, the
 * current world needs to be reloaded. When starting MegaZeux, if the startup
 * world can't be loaded, the load menu needs to be displayed instead.
 */

static void title_resume(context *ctx)
{
  struct game_context *title = (struct game_context *)ctx;
  struct config_info *conf = get_config(ctx);

  if(title->need_reload)
  {
    if(!load_world_title(title, curr_file))
    {
      conf->standalone_mode = false;

      if(title->load_dialog_on_failed_load)
      {
        load_world_title_selection(title);
      }
    }
  }

  title->load_dialog_on_failed_load = false;
  title->need_reload = false;
}

/**
 * Key handler for the title screen.
 */

static boolean title_key(context *ctx, int *key)
{
  struct game_context *title = (struct game_context *)ctx;
  struct config_info *conf = get_config(ctx);
  struct world *mzx_world = ctx->world;

  int key_status = get_key_status(keycode_internal_wrt_numlock, *key);
  boolean exit_status = get_exit_status();

  boolean reload_curr_file_in_editor = true;
  boolean confirm_exit = false;

  switch(*key)
  {
#ifdef CONFIG_HELPSYS
    case IKEY_h:
    {
      // Help system alternate binding.
      *key = IKEY_F1;
      break;
    }
#endif

    case IKEY_s:
    {
      // Configure alternate binding.
      *key = IKEY_F2;
      break;
    }

    case IKEY_F3:
    case IKEY_l:
    {
      if(conf->standalone_mode)
        return true;

      load_world_title_selection(title);
      return true;
    }

    case IKEY_F4:
    {
      // ALT+F4 - do nothing.
      if(get_alt_status(keycode_internal))
        break;
    }

    /* fallthrough */

    case IKEY_r:
    {
      // Restore saved game
      if(conf->standalone_mode && !get_counter(mzx_world, "LOAD_MENU", 0))
        return true;

      if(load_savegame_selection(title))
      {
        play_game(ctx, &(title->fade_in));
        title->need_reload = true;
      }
      return true;
    }

    case IKEY_F5:
    case IKEY_p:
    {
      // Play game
      if(mzx_world->active)
      {
        if(mzx_world->only_from_swap)
        {
          error("You can only play this game via a swap"
           " from another game", 0, 24, 0x3101);
          return true;
        }

        if(load_world_gameplay(title, curr_file))
        {
          play_game(ctx, NULL);
          title->need_reload = true;
        }
      }
      return true;
    }

    case IKEY_F7:
    case IKEY_u:
    {
      if(check_for_updates)
      {
        // FIXME this is garbage
        int current_music_vol = audio_get_music_volume();
        int current_pcs_vol = audio_get_pcs_volume();
        audio_set_music_volume(0);
        audio_set_pcs_volume(0);
        if(mzx_world->active)
          audio_set_module_volume(0);

        check_for_updates(mzx_world, conf, 0);

        audio_set_pcs_volume(current_pcs_vol);
        audio_set_music_volume(current_music_vol);
        if(mzx_world->active)
          audio_set_module_volume(mzx_world->current_board->volume);
      }
      return true;
    }

    case IKEY_F8:
    case IKEY_n:
    {
      reload_curr_file_in_editor = false;
    }

    /* fallthrough */

    case IKEY_F9:
    case IKEY_e:
    {
      if(edit_world)
      {
        // Editor
        sfx_clear_queue();
        vquick_fadeout();
        title->need_reload = true;
        title->fade_in = true;

        edit_world(ctx, reload_curr_file_in_editor);
      }
      return true;
    }

    // Quickload saved game
    case IKEY_F10:
    {
      struct stat file_info;

      if(conf->standalone_mode && !get_counter(mzx_world, "LOAD_MENU", 0))
        return true;

      if(!stat(curr_sav, &file_info) && load_savegame(title, curr_sav))
      {
        play_game(ctx, &(title->fade_in));
        title->need_reload = true;
      }
      return true;
    }

    case IKEY_RETURN: // Enter
    {
      // Ignore if this isn't a fresh press
      if(key_status != 1)
        return true;

      if(!conf->standalone_mode || get_counter(mzx_world, "ENTER_MENU", 0))
        main_menu(ctx);

      return true;
    }

    case IKEY_ESCAPE:
    {
      // Ignore if this isn't a fresh press
      if(key_status != 1)
        return true;

      // ESCAPE_MENU (2.90+) only works on the title screen if the
      // standalone_mode config option is set
      if(mzx_world->version < V290 || !conf->standalone_mode ||
       get_counter(mzx_world, "ESCAPE_MENU", 0))
        confirm_exit = true;

      break;
    }
  }

  // Quit
  if(exit_status || confirm_exit)
  {
    // Special behaviour in standalone- only escape exits
    // ask for confirmation. Exit events instead terminate MegaZeux.
    if(conf->standalone_mode && !confirm_exit)
    {
      core_exit(ctx);
    }
    else
    {
      m_show();

      if(!confirm(mzx_world, "Exit MegaZeux - Are you sure?"))
        destroy_context(ctx);
    }
    return true;
  }

  return false;
}

/**
 * Create and run the title screen context. This context should only be started
 * when MegaZeux is opened, and MegaZeux should terminate if it is destroyed.
 * If startup_editor is enabled, also start the editor. If standalone_mode and
 * no_titlescreen mode are enabled, only attempt to start gameplay instead.
 */

void title_screen(context *parent)
{
  struct config_info *conf = get_config(parent);
  struct game_context *title;
  struct context_spec spec;

  if(edit_world)
  {
    conf->standalone_mode = false;
  }

  if(conf->standalone_mode && conf->no_titlescreen)
  {
    struct game_context dummy;
    dummy.ctx.world = parent->world;
    dummy.ctx.data = parent->data;

    if(load_world_gameplay(&dummy, curr_file))
    {
      play_game(parent, NULL);
      return;
    }

    conf->standalone_mode = false;
  }

  title = cmalloc(sizeof(struct game_context));
  title->fade_in = true;
  title->need_reload = true;
  title->load_dialog_on_failed_load = true;
  title->is_title = true;

  memset(&spec, 0, sizeof(struct context_spec));
  spec.resume   = title_resume;
  spec.draw     = game_draw;
  spec.idle     = game_idle;
  spec.key      = title_key;
  spec.destroy  = game_destroy;

  create_context((context *)title, parent, &spec, CTX_TITLE_SCREEN);

  if(edit_world && conf->startup_editor)
  {
    title->load_dialog_on_failed_load = false;
    edit_world((context *)title, true);
  }

  default_palette();
  clear_screen();
}
