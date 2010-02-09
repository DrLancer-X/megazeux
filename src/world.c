/* MegaZeux
 *
 * Copyright (C) 1996 Greg Janson
 * Copyright (C) 1999 Charles Goetzman
 * Copyright (C) 2004 Gilead Kutnick <exophase@adelphia.net>
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "configure.h"
#include "helpsys.h"
#include "sfx.h"
#include "scrdisp.h"
#include "error.h"
#include "window.h"
#include "const.h"
#include "counter.h"
#include "sprite.h"
#include "counter.h"
#include "graphics.h"
#include "event.h"
#include "world.h"
#include "data.h"
#include "idput.h"
#include "decrypt.h"
#include "fsafeopen.h"
#include "game.h"
#include "audio.h"
#include "extmem.h"
#include "util.h"

// Get 2 bytes

int fgetw(FILE *fp)
{
  int r = fgetc(fp);
  r |= fgetc(fp) << 8;
  return r;
}

// Get 4 bytes

int fgetd(FILE *fp)
{
  int r = fgetc(fp);
  r |= fgetc(fp) << 8;
  r |= fgetc(fp) << 16;
  r |= fgetc(fp) << 24;

  return r;
}

// Put 2 bytes

void fputw(int src, FILE *fp)
{
  fputc(src & 0xFF, fp);
  fputc(src >> 8, fp);
}

// Put 4 bytes

void fputd(int src, FILE *fp)
{
  fputc(src & 0xFF, fp);
  fputc((src >> 8) & 0xFF, fp);
  fputc((src >> 16) & 0xFF, fp);
  fputc((src >> 24) & 0xFF, fp);
}

__editor_maybe_static int world_magic(const char magic_string[3])
{
  if(magic_string[0] == 'M')
  {
    if(magic_string[1] == 'Z')
    {
      switch(magic_string[2])
      {
        case 'X':
          return 0x0100;
        case '2':
          return 0x0205;
        case 'A':
          return 0x0208;
        default:
          return 0;
      }
    }
    else
    {
      if((magic_string[1] > 1) && (magic_string[1] < 10))
      {
        // I hope to God that MZX doesn't last beyond 9.x
        return ((int)magic_string[1] << 8) + (int)magic_string[2];
      }
      else
      {
        return 0;
      }
    }
  }
  else
  {
    return 0;
  }
}

#ifdef CONFIG_LOADSAVE_METER

static void meter_update_screen(World *mzx_world, int *curr, int target)
{
  (*curr)++;
  meter_interior(*curr, target);
  update_screen();
}

static void meter_restore_screen(World *mzx_world)
{
  restore_screen();
  update_screen();
}

static void meter_initial_draw(World *mzx_world, int curr, int target,
 const char *title)
{
  save_screen();
  meter(title, curr, target);
  update_screen();
}

#else // !CONFIG_LOADSAVE_METER

static inline void meter_update_screen(World *mzx_world, int *curr,
 int target) {}
static inline void meter_restore_screen(World *mzx_world) {}
static void meter_initial_draw(World *mzx_world, int curr, int target,
 const char *title) {}

#endif // CONFIG_LOADSAVE_METER

int save_world(World *mzx_world, const char *file, int savegame, int faded)
{
  int i, num_boards;
  int gl_rob_position, gl_rob_save_position;
  int board_offsets_position, board_begin_position, board_end_position;
  int board_size;
  unsigned int *size_offset_list;
  unsigned char *charset_mem;
  unsigned char r, g, b;
  Board *cur_board;

  int meter_target = 2 + mzx_world->num_boards, meter_curr = 0;

  FILE *fp = fopen(file, "wb");

  if(fp == NULL)
  {
    error("Error saving world", 1, 8, 0x0D01);
    return -1;
  }

  meter_initial_draw(mzx_world, meter_curr, meter_target, "Saving...");

  if(savegame)
  {
    // Write this MZX's version string
    fputs("MZS", fp);
    fputc((WORLD_VERSION >> 8) & 0xff, fp);
    fputc(WORLD_VERSION & 0xff, fp);

    // Write the version of the loaded world for this SAV
    fputw(mzx_world->version, fp);

    fputc(mzx_world->current_board_id, fp);
  }
  else
  {
    fwrite(mzx_world->name, BOARD_NAME_SIZE, 1, fp);

    // No protection
    fputc(0, fp);

    // Write this MZX's version string
    fputc('M', fp);
    fputc((WORLD_VERSION >> 8) & 0xff, fp);
    fputc(WORLD_VERSION & 0xff, fp);
  }

  // Save charset
  charset_mem = malloc(3584);
  ec_mem_save_set(charset_mem);
  fwrite(charset_mem, 3584, 1, fp);
  free(charset_mem);

  // Save idchars array.
  fwrite(id_chars, 323, 1, fp);
  fputc(missile_color, fp);
  fwrite(bullet_color, 3, 1, fp);
  fwrite(id_dmg, 128, 1, fp);

  // Save status counters.
  fwrite((char *)mzx_world->status_counters_shown, COUNTER_NAME_SIZE,
   NUM_STATUS_COUNTERS, fp);

  /* Older MZX sources refer to SAVE_INDIVIDUAL, but it has always been
   * defined. Exo eventually removed the conditional code in 2.80.
   * We don't need to think about it.
   */

  if(savegame)
  {
    fwrite(mzx_world->keys, NUM_KEYS, 1, fp);
    fputd(mzx_world->score, fp);
    fputc(mzx_world->blind_dur, fp);
    fputc(mzx_world->firewalker_dur, fp);
    fputc(mzx_world->freeze_time_dur, fp);
    fputc(mzx_world->slow_time_dur, fp);
    fputc(mzx_world->wind_dur, fp);

    for(i = 0; i < 8; i++)
    {
      fputw(mzx_world->pl_saved_x[i], fp);
    }

    for(i = 0; i < 8; i++)
    {
      fputw(mzx_world->pl_saved_y[i], fp);
    }

    fwrite(mzx_world->pl_saved_board, 8, 1, fp);
    fputc(mzx_world->saved_pl_color, fp);
    fputc(mzx_world->under_player_id, fp);
    fputc(mzx_world->under_player_color, fp);
    fputc(mzx_world->under_player_param, fp);
    fputc(mzx_world->mesg_edges, fp);
    fputc(mzx_world->scroll_base_color, fp);
    fputc(mzx_world->scroll_corner_color, fp);
    fputc(mzx_world->scroll_pointer_color, fp);
    fputc(mzx_world->scroll_title_color, fp);
    fputc(mzx_world->scroll_arrow_color, fp);
    fwrite(mzx_world->real_mod_playing, 13, 1, fp);
  }

  fputc(mzx_world->edge_color, fp);
  fputc(mzx_world->first_board, fp);
  fputc(mzx_world->endgame_board, fp);
  fputc(mzx_world->death_board, fp);
  fputw(mzx_world->endgame_x, fp);
  fputw(mzx_world->endgame_y, fp);
  fputc(mzx_world->game_over_sfx, fp);
  fputw(mzx_world->death_x, fp);
  fputw(mzx_world->death_y, fp);
  fputw(mzx_world->starting_lives, fp);
  fputw(mzx_world->lives_limit, fp);
  fputw(mzx_world->starting_health, fp);
  fputw(mzx_world->health_limit, fp);
  fputc(mzx_world->enemy_hurt_enemy, fp);
  fputc(mzx_world->clear_on_exit, fp);
  fputc(mzx_world->only_from_swap, fp);

  // Palette...
  for(i = 0; i < 16; i++)
  {
    get_rgb(i, &r, &g, &b);
    fputc(r, fp);
    fputc(g, fp);
    fputc(b, fp);
  }

  if(savegame)
  {
    int vlayer_size;

    for(i = 0; i < 16; i++)
    {
      fputc(get_color_intensity(i), fp);
    }
    fputc(faded, fp);

    fputw(mzx_world->player_restart_x, fp);
    fputw(mzx_world->player_restart_y, fp);
    fputc(mzx_world->under_player_id, fp);
    fputc(mzx_world->under_player_color, fp);
    fputc(mzx_world->under_player_param, fp);

    // Write counters
    fputd(mzx_world->num_counters, fp);
    for(i = 0; i < mzx_world->num_counters; i++)
    {
      save_counter(fp, mzx_world->counter_list[i]);
    }

    // Write strings
    fputd(mzx_world->num_strings, fp);

    for(i = 0; i < mzx_world->num_strings; i++)
    {
      save_string(fp, mzx_world->string_list[i]);
    }

    // Sprite data
    for(i = 0; i < MAX_SPRITES; i++)
    {
      fputw((mzx_world->sprite_list[i])->x, fp);
      fputw((mzx_world->sprite_list[i])->y, fp);
      fputw((mzx_world->sprite_list[i])->ref_x, fp);
      fputw((mzx_world->sprite_list[i])->ref_y, fp);
      fputc((mzx_world->sprite_list[i])->color, fp);
      fputc((mzx_world->sprite_list[i])->flags, fp);
      fputc((mzx_world->sprite_list[i])->width, fp);
      fputc((mzx_world->sprite_list[i])->height, fp);
      fputc((mzx_world->sprite_list[i])->col_x, fp);
      fputc((mzx_world->sprite_list[i])->col_y, fp);
      fputc((mzx_world->sprite_list[i])->col_width, fp);
      fputc((mzx_world->sprite_list[i])->col_height, fp);
    }
    // total sprites
    fputc(mzx_world->active_sprites, fp);
    // y order flag
    fputc(mzx_world->sprite_y_order, fp);
    // collision info
    fputw(mzx_world->collision_count, fp);

    for(i = 0; i < MAX_SPRITES; i++)
    {
      fputw(mzx_world->collision_list[i], fp);
    }

    // Multiplier
    fputw(mzx_world->multiplier, fp);
    // Divider
    fputw(mzx_world->divider, fp);
    // Circle divisions
    fputw(mzx_world->c_divisions, fp);
    // Builtin message status
    fputc(mzx_world->bi_mesg_status, fp);

    // Write input file name and if open, size
    fwrite(mzx_world->input_file_name, 1, 12, fp);
    if(mzx_world->input_file)
    {
      fputd(ftell(mzx_world->input_file), fp);
    }
    else
    {
      fputd(0, fp);
    }

    // Write output file name and if open, size
    fwrite(mzx_world->output_file_name, 1, 12, fp);
    if(mzx_world->output_file)
    {
      fputd(ftell(mzx_world->output_file), fp);
    }
    else
    {
      fputd(0, fp);
    }

    fputw(get_screen_mode(), fp);

    if(get_screen_mode() > 1)
    {
      // Put SMZX mode 2 palette
      for(i = 0; i < 256; i++)
      {
        get_rgb(i, &r, &g, &b);
        fputc(r, fp);
        fputc(g, fp);
        fputc(b, fp);
      }
    }

    fputw(mzx_world->commands, fp);

    vlayer_size = mzx_world->vlayer_size;
    fputd(vlayer_size, fp);
    fputw(mzx_world->vlayer_width, fp);
    fputw(mzx_world->vlayer_height, fp);

    fwrite(mzx_world->vlayer_chars, 1, vlayer_size, fp);
    fwrite(mzx_world->vlayer_colors, 1, vlayer_size, fp);
  }

  // Put position of global robot later
  gl_rob_save_position = ftell(fp);
  // Put some 0's
  fputd(0, fp);

  // Put custom fx?
  if(mzx_world->custom_sfx_on == 1)
  {
    int offset = 0;
    int sfx_len;
    int length_slot_pos, next_pos, total_len;
    fputc(0, fp);
    length_slot_pos = ftell(fp);
    fputw(0, fp);
    for(i = 0; i < NUM_SFX; i++, offset += 69)
    {
      sfx_len = strlen(mzx_world->custom_sfx + offset);
      fputc(sfx_len, fp);
      fwrite(mzx_world->custom_sfx + offset, sfx_len, 1, fp);
    }
    // Get size of the block
    next_pos = ftell(fp);
    total_len = (next_pos - length_slot_pos) - 2;
    fseek(fp, length_slot_pos, SEEK_SET);
    fputw(total_len, fp);
    fseek(fp, next_pos, SEEK_SET);
  }

  meter_update_screen(mzx_world, &meter_curr, meter_target);

  num_boards = mzx_world->num_boards;
  fputc(num_boards, fp);

  // Put the names
  for(i = 0; i < num_boards; i++)
  {
    fwrite((mzx_world->board_list[i])->board_name, 25, 1, fp);
  }

  /* Due to some bugs in the NDS's libfat library, seeking backwards
   * from the end results in data corruption. To prevent this, waste
   * a little bit of memory caching the offsets of the board data so
   * we can rewrite the size/offset list with less seeking later.
   */
  size_offset_list = malloc(8 * num_boards);
  board_offsets_position = ftell(fp);
  fseek(fp, 8 * num_boards, SEEK_CUR);

  for(i = 0; i < num_boards; i++)
  {
    cur_board = mzx_world->board_list[i];

    // Before messing with the board, make sure the board is
    // rid of any gaps in the object lists...
    optimize_null_objects(cur_board);

    // First save the offset of where the board will be placed
    board_begin_position = ftell(fp);
    // Now save the board and get the size
    board_size = save_board(cur_board, fp, savegame);
    // Save where the next board should go
    board_end_position = ftell(fp);
    // Record size/offset information.
    size_offset_list[2 * i] = board_size;
    size_offset_list[2 * i + 1] = board_begin_position;

    meter_update_screen(mzx_world, &meter_curr, meter_target);
  }

  // Save for global robot position
  gl_rob_position = ftell(fp);
  save_robot(mzx_world->global_robot, fp, savegame);

  meter_update_screen(mzx_world, &meter_curr, meter_target);

  // Go back to where the global robot position should be saved
  fseek(fp, gl_rob_save_position, SEEK_SET);
  fputd(gl_rob_position, fp);

  // Go back to offsets/size list
  fseek(fp, board_offsets_position, SEEK_SET);
  for(i = 0; i < num_boards; i++)
  {
    fputd(size_offset_list[2 * i  ], fp);
    fputd(size_offset_list[2 * i + 1], fp);
  }
  free(size_offset_list);

  // ...All done!
  fclose(fp);

  meter_restore_screen(mzx_world);
  return 0;
}

static int save_magic(const char magic_string[5])
{
  if((magic_string[0] == 'M') && (magic_string[1] == 'Z'))
  {
    switch(magic_string[2])
    {
      case 'S':
        if((magic_string[3] == 'V') && (magic_string[4] == '2'))
        {
          return 0x0205;
        }
        else if((magic_string[3] >= 2) && (magic_string[3] <= 10))
        {
          return((int)magic_string[3] << 8 ) + magic_string[4];
        }
        else
        {
          return 0;
        }
      case 'X':
        if((magic_string[3] == 'S') && (magic_string[4] == 'A'))
        {
          return 0x0208;
        }
        else
        {
          return 0;
        }
      default:
        return 0;
    }
  }
  else
  {
    return 0;
  }
}

__editor_maybe_static void set_update_done(World *mzx_world)
{
  Board **board_list = mzx_world->board_list;
  Board *cur_board;
  int max_size = 0;
  int cur_size;
  int i;

  for(i = 0; i < mzx_world->num_boards; i++)
  {
    cur_board = board_list[i];
    cur_size = cur_board->board_width * cur_board->board_height;

    if(cur_size > max_size)
      max_size = cur_size;
  }

  if(max_size > mzx_world->update_done_size)
  {
    if(mzx_world->update_done == NULL)
    {
      mzx_world->update_done = malloc(max_size);
    }
    else
    {
      mzx_world->update_done =
       realloc(mzx_world->update_done, max_size);
    }

    mzx_world->update_done_size = max_size;
  }
}

__editor_maybe_static void optimize_null_boards(World *mzx_world)
{
  // Optimize out null objects while keeping a translation list mapping
  // board numbers from the old list to the new list.
  int num_boards = mzx_world->num_boards;
  Board **board_list = mzx_world->board_list;
  Board **optimized_board_list = calloc(num_boards, sizeof(Board *));
  int *board_id_translation_list = calloc(num_boards, sizeof(int));

  Board *cur_board;
  int i, i2;

  for(i = 0, i2 = 0; i < num_boards; i++)
  {
    cur_board = board_list[i];
    if(cur_board != NULL)
    {
      optimized_board_list[i2] = cur_board;
      board_id_translation_list[i] = i2;
      i2++;
    }
    else
    {
      board_id_translation_list[i] = NO_BOARD;
    }
  }

  // Might need to fix these first, to correct old MZX games.
  if(mzx_world->first_board >= num_boards)
    mzx_world->first_board = 0;

  if((mzx_world->death_board >= num_boards) &&
   (mzx_world->death_board < DEATH_SAME_POS))
    mzx_world->death_board = NO_BOARD;

  if(mzx_world->endgame_board >= num_boards)
    mzx_world->endgame_board = NO_BOARD;

  if(i2 < num_boards)
  {
    int offset;
    char *level_id;
    char *level_param;
    int d_param, d_flag;
    int board_width;
    int board_height;
    int relocate_current = 1;
    int i3;

    if(board_list[mzx_world->current_board_id] == NULL)
      relocate_current = 0;

    free(board_list);
    board_list =
     realloc(optimized_board_list, sizeof(Board *) * i2);

    mzx_world->num_boards = i2;
    mzx_world->num_boards_allocated = i2;

    // Fix all entrances and exits in each board
    for(i = 0; i < i2; i++)
    {
      cur_board = board_list[i];
      board_width = cur_board->board_width;
      board_height = cur_board->board_height;
      level_id = cur_board->level_id;
      level_param = cur_board->level_param;

      // Fix entrances
      for(offset = 0; offset < board_width * board_height; offset++)
      {
        d_flag = flags[(int)level_id[offset]];

        if(d_flag & A_ENTRANCE)
        {
          d_param = level_param[offset];
          if(d_param < num_boards)
            level_param[offset] = board_id_translation_list[d_param];
          else
            level_param[offset] = NO_BOARD;
        }
      }

      // Fix exits
      for(i3 = 0; i3 < 4; i3++)
      {
        d_param = cur_board->board_dir[i3];

        if(d_param < i2)
          cur_board->board_dir[i3] = board_id_translation_list[d_param];
        else
          cur_board->board_dir[i3] = NO_BOARD;
      }
    }

    // Fix current board
    if(relocate_current)
    {
      d_param = mzx_world->current_board_id;
      d_param = board_id_translation_list[d_param];
      mzx_world->current_board_id = d_param;
      mzx_world->current_board = board_list[d_param];
    }

    d_param = mzx_world->first_board;
    if(d_param >= num_boards)
      d_param = num_boards - 1;

    d_param = board_id_translation_list[d_param];
    mzx_world->first_board = d_param;

    d_param = mzx_world->endgame_board;

    if(d_param != NO_BOARD)
    {
      if(d_param >= num_boards)
        d_param = num_boards - 1;

      d_param = board_id_translation_list[d_param];
      mzx_world->endgame_board = d_param;
    }

    d_param = mzx_world->death_board;

    if((d_param != NO_BOARD) && (d_param != DEATH_SAME_POS))
    {
      if(d_param >= num_boards)
        d_param = num_boards - 1;

      d_param = board_id_translation_list[d_param];
      mzx_world->death_board = d_param;
    }

    mzx_world->board_list = board_list;
  }
  else
  {
    free(optimized_board_list);
  }

  free(board_id_translation_list);
}

// Loads a world into a World struct

static int load_world(World *mzx_world, const char *file, int savegame,
 int *faded)
{
  int version;
  int i;
  int num_boards;
  int gl_rob, last_pos;
  char tempstr[16];
  unsigned char *charset_mem;
  unsigned char r, g, b;
  Board *cur_board;
  char *config_file_name;
  int file_name_len = strlen(file) - 4;
  struct stat file_info;
  char *file_path;
  char *current_dir;
  int ret = 1;
  FILE *fp;

  int meter_target = 2, meter_curr = 0;

  fp = fopen(file, "rb");
  if(fp == NULL)
  {
    error("Error loading world", 1, 8, 0x0D01);
    goto exit_out;
  }

  if(savegame)
  {
    fread(tempstr, 1, 5, fp);

    version = save_magic(tempstr);

    if(version != WORLD_VERSION)
    {
      const char *msg;

      if(version > WORLD_VERSION)
        msg = ".SAV files from newer versions of MZX are not supported";
      else if(version < WORLD_VERSION)
        msg = ".SAV files from older versions of MZX are not supported";
      else
        msg = "Unrecognized magic: file may not be .SAV file";

      error(msg, 1, 8, 0x2101);
      goto exit_close;
    }

    if(!mzx_world)
      goto exit_ok_close;

    mzx_world->version = fgetw(fp);
    mzx_world->current_board_id = fgetc(fp);
  }
  else
  {
    int protection_method;
    char magic[3];
    char error_string[80];

    if(mzx_world)
    {
      // Name of game- skip it.
      fread(mzx_world->name, BOARD_NAME_SIZE, 1, fp);
    }
    else
    {
      /* In the case where mzx_world is NULL, it means we only want to
       * CHECK the world, we don't want to actually load anything. In
       * this case, we can simply skip the game name.
       */
      fseek(fp, BOARD_NAME_SIZE, SEEK_CUR);
    }

    // Skip protection method
    protection_method = fgetc(fp);

    if(protection_method)
    {
      /* Again, if we're just checking the world, we don't have a
       * mzx_world with which to ask the user about decrypting. So in
       * this case, we just fail if the world is encrypted.
       */
      if(mzx_world)
      {
        int do_decrypt;

        error("This world is password protected.", 1, 8, 0x0D02);
        do_decrypt = confirm(mzx_world, "Would you like to decrypt it?");
        if(!do_decrypt)
        {
          fclose(fp);
          decrypt(file);
          goto exit_recurse;
        }
      }

      error("Cannot load password protected world.", 1, 8, 0x0D02);
      goto exit_close;
    }

    fread(magic, 1, 3, fp);
    version = world_magic(magic);
    if(version == 0)
    {
      sprintf(error_string, "Attempt to load non-MZX world.");
    }
    else

    if(version < 0x0205)
    {
      sprintf(error_string, "World is from old version (%d.%d); use converter",
       (version & 0xFF00) >> 8, version & 0xFF);
      version = 0;
    }
    else

    if(version > WORLD_VERSION)
    {
      sprintf(error_string, "World is from more recent version (%d.%d)",
       (version & 0xFF00) >> 8, version & 0xFF);
      version = 0;
    }

    if(!version)
    {
      error(error_string, 1, 8, 0x0D02);
      goto exit_close;
    }

    if(!mzx_world)
      goto exit_ok_close;

    mzx_world->current_board_id = 0;
    mzx_world->version = version;
  }

  meter_initial_draw(mzx_world, meter_curr, meter_target, "Loading...");

  file_path = malloc(MAX_PATH);
  current_dir = malloc(MAX_PATH);
  config_file_name = malloc(MAX_PATH);

  get_path(file, file_path, MAX_PATH);

  if(file_path[0])
  {
    getcwd(current_dir, MAX_PATH);

    if(strcmp(current_dir, file_path))
      chdir(file_path);
  }

  memcpy(config_file_name, file, file_name_len);
  strncpy(config_file_name + file_name_len, ".cnf", 5);

  if(stat(config_file_name, &file_info) >= 0)
  {
    set_config_from_file(&(mzx_world->conf), config_file_name);
  }

  free(config_file_name);
  free(current_dir);
  free(file_path);

  charset_mem = malloc(3584);
  fread(charset_mem, 3584, 1, fp);
  ec_mem_load_set(charset_mem);
  free(charset_mem);

  // Idchars array...
  fread(id_chars, 323, 1, fp);
  missile_color = fgetc(fp);
  fread(bullet_color, 3, 1, fp);
  fread(id_dmg, 128, 1, fp);

  // Status counters...
  fread((char *)mzx_world->status_counters_shown, COUNTER_NAME_SIZE,
   NUM_STATUS_COUNTERS, fp);

  if(savegame)
  {
    fread(mzx_world->keys, NUM_KEYS, 1, fp);
    mzx_world->score = fgetd(fp);
    mzx_world->blind_dur = fgetc(fp);
    mzx_world->firewalker_dur = fgetc(fp);
    mzx_world->freeze_time_dur = fgetc(fp);
    mzx_world->slow_time_dur = fgetc(fp);
    mzx_world->wind_dur = fgetc(fp);

    for(i = 0; i < 8; i++)
    {
      mzx_world->pl_saved_x[i] = fgetw(fp);
    }

    for(i = 0; i < 8; i++)
    {
      mzx_world->pl_saved_y[i] = fgetw(fp);
    }

    fread(mzx_world->pl_saved_board, 8, 1, fp);
    mzx_world->saved_pl_color = fgetc(fp);
    mzx_world->under_player_id = fgetc(fp);
    mzx_world->under_player_color = fgetc(fp);
    mzx_world->under_player_param = fgetc(fp);
    mzx_world->mesg_edges = fgetc(fp);
    mzx_world->scroll_base_color = fgetc(fp);
    mzx_world->scroll_corner_color = fgetc(fp);
    mzx_world->scroll_pointer_color = fgetc(fp);
    mzx_world->scroll_title_color = fgetc(fp);
    mzx_world->scroll_arrow_color = fgetc(fp);
    fread(mzx_world->real_mod_playing, 13, 1, fp);
  }

  mzx_world->edge_color = fgetc(fp);
  mzx_world->first_board = fgetc(fp);
  mzx_world->endgame_board = fgetc(fp);
  mzx_world->death_board = fgetc(fp);
  mzx_world->endgame_x = fgetw(fp);
  mzx_world->endgame_y = fgetw(fp);
  mzx_world->game_over_sfx = fgetc(fp);
  mzx_world->death_x = fgetw(fp);
  mzx_world->death_y = fgetw(fp);
  mzx_world->starting_lives = fgetw(fp);
  mzx_world->lives_limit = fgetw(fp);
  mzx_world->starting_health = fgetw(fp);
  mzx_world->health_limit = fgetw(fp);
  mzx_world->enemy_hurt_enemy = fgetc(fp);
  mzx_world->clear_on_exit = fgetc(fp);
  mzx_world->only_from_swap = fgetc(fp);

  // Palette...
  for(i = 0; i < 16; i++)
  {
    r = fgetc(fp);
    g = fgetc(fp);
    b = fgetc(fp);

    set_rgb(i, r, g, b);
  }

  if(savegame)
  {
    int vlayer_size;
    int num_counters, num_strings;
    int screen_mode;

    for(i = 0; i < 16; i++)
    {
      set_color_intensity(i, fgetc(fp));
    }

    *faded = fgetc(fp);

    mzx_world->player_restart_x = fgetw(fp);
    mzx_world->player_restart_y = fgetw(fp);
    mzx_world->under_player_id = fgetc(fp);
    mzx_world->under_player_color = fgetc(fp);
    mzx_world->under_player_param = fgetc(fp);

    // Read counters
    num_counters = fgetd(fp);
    mzx_world->num_counters = num_counters;
    mzx_world->num_counters_allocated = num_counters;
    mzx_world->counter_list = calloc(num_counters, sizeof(counter *));

    for(i = 0; i < num_counters; i++)
    {
      mzx_world->counter_list[i] = load_counter(fp);
    }

    // Setup gateway functions
    initialize_gateway_functions(mzx_world);

    // Read mzx_strings
    num_strings = fgetd(fp);
    mzx_world->num_strings = num_strings;
    mzx_world->num_strings_allocated = num_strings;
    mzx_world->string_list = calloc(num_strings, sizeof(mzx_string *));

    for(i = 0; i < num_strings; i++)
    {
      mzx_world->string_list[i] = load_string(fp);
    }

    // Allocate space for sprites and clist
    mzx_world->num_sprites = MAX_SPRITES;
    mzx_world->sprite_list = calloc(MAX_SPRITES, sizeof(Sprite *));

    for(i = 0; i < MAX_SPRITES; i++)
    {
      mzx_world->sprite_list[i] = calloc(1, sizeof(Sprite));
    }

    mzx_world->collision_list = calloc(MAX_SPRITES, sizeof(int));
    mzx_world->sprite_num = 0;

    // Sprite data
    for(i = 0; i < MAX_SPRITES; i++)
    {
      (mzx_world->sprite_list[i])->x = fgetw(fp);
      (mzx_world->sprite_list[i])->y = fgetw(fp);
      (mzx_world->sprite_list[i])->ref_x = fgetw(fp);
      (mzx_world->sprite_list[i])->ref_y = fgetw(fp);
      (mzx_world->sprite_list[i])->color = fgetc(fp);
      (mzx_world->sprite_list[i])->flags = fgetc(fp);
      (mzx_world->sprite_list[i])->width = fgetc(fp);
      (mzx_world->sprite_list[i])->height = fgetc(fp);
      (mzx_world->sprite_list[i])->col_x = fgetc(fp);
      (mzx_world->sprite_list[i])->col_y = fgetc(fp);
      (mzx_world->sprite_list[i])->col_width = fgetc(fp);
      (mzx_world->sprite_list[i])->col_height = fgetc(fp);
    }

    // total sprites
    mzx_world->active_sprites = fgetc(fp);
    // y order flag
    mzx_world->sprite_y_order = fgetc(fp);
    // collision info
    mzx_world->collision_count = fgetw(fp);

    for(i = 0; i < MAX_SPRITES; i++)
    {
      mzx_world->collision_list[i] = fgetw(fp);
    }

    // Multiplier
    mzx_world->multiplier = fgetw(fp);
    // Divider
    mzx_world->divider = fgetw(fp);
    // Circle divisions
    mzx_world->c_divisions = fgetw(fp);
    // Builtin message status
    mzx_world->bi_mesg_status = fgetc(fp);

    // Load input file name, open
    fread(mzx_world->input_file_name, 1, 12, fp);
    mzx_world->input_file_name[12] = 0;
    if(mzx_world->input_file_name[0] != '\0')
    {
      mzx_world->input_file =
       fsafeopen(mzx_world->input_file_name, "rb");

      if(mzx_world->input_file)
      {
        fseek(mzx_world->input_file, fgetd(fp), SEEK_SET);
      }
      else
      {
        fseek(fp, 4, SEEK_CUR);
      }
    }
    else
    {
      fseek(fp, 4, SEEK_CUR);
    }

    // Load ouput file name, open
    fread(mzx_world->output_file_name, 1, 12, fp);
    mzx_world->output_file_name[12] = 0;
    if(mzx_world->output_file_name[0] != '\0')
    {
      mzx_world->output_file =
       fsafeopen(mzx_world->output_file_name, "ab");

      if(mzx_world->output_file)
      {
        fseek(mzx_world->output_file, fgetd(fp), SEEK_SET);
      }
      else
      {
        fseek(fp, 4, SEEK_CUR);
      }
    }
    else
    {
      fseek(fp, 4, SEEK_CUR);
    }

    screen_mode = fgetw(fp);

    // If it's at SMZX mode 2, set default palette as loaded
    // so the .sav one doesn't get overwritten
    if(screen_mode == 2)
    {
      smzx_palette_loaded(1);
    }
    set_screen_mode(screen_mode);

    // Also get the palette
    if(screen_mode > 1)
    {
      for(i = 0; i < 256; i++)
      {
        r = fgetc(fp);
        g = fgetc(fp);
        b = fgetc(fp);

        set_rgb(i, r, g, b);
      }
    }

    mzx_world->commands = fgetw(fp);

    vlayer_size = fgetd(fp);
    mzx_world->vlayer_width = fgetw(fp);
    mzx_world->vlayer_height = fgetw(fp);
    mzx_world->vlayer_size = vlayer_size;

    mzx_world->vlayer_chars = malloc(vlayer_size);
    mzx_world->vlayer_colors = malloc(vlayer_size);

    fread(mzx_world->vlayer_chars, 1, vlayer_size, fp);
    fread(mzx_world->vlayer_colors, 1, vlayer_size, fp);
  }

  update_palette();

  // Get position of global robot...
  gl_rob = fgetd(fp);
  // Get number of boards
  num_boards = fgetc(fp);

  if(num_boards == 0)
  {
    int sfx_size;
    char *sfx_offset = mzx_world->custom_sfx;
    // Sfx
    mzx_world->custom_sfx_on = 1;
    fseek(fp, 2, SEEK_CUR);     // Skip word size

    //Read sfx
    for(i = 0; i < NUM_SFX; i++, sfx_offset += 69)
    {
      sfx_size = fgetc(fp);
      fread(sfx_offset, sfx_size, 1, fp);
    }
    num_boards = fgetc(fp);
  }
  else
  {
    mzx_world->custom_sfx_on = 0;
  }

  meter_target += num_boards;
  meter_update_screen(mzx_world, &meter_curr, meter_target);

  mzx_world->num_boards = num_boards;
  mzx_world->num_boards_allocated = num_boards;
  mzx_world->board_list = malloc(sizeof(Board *) * num_boards);

  // Skip the names for now
  // Gonna wanna come back to here
  last_pos = ftell(fp);
  fseek(fp, num_boards * BOARD_NAME_SIZE, SEEK_CUR);

  for(i = 0; i < num_boards; i++)
  {
    mzx_world->board_list[i] = load_board_allocate(fp, savegame);
    store_board_to_extram(mzx_world->board_list[i]);
    meter_update_screen(mzx_world, &meter_curr, meter_target);
  }

  // Read global robot
  fseek(fp, gl_rob, SEEK_SET);
  load_robot(mzx_world->global_robot, fp, savegame);

  // Go back to where the names are
  fseek(fp, last_pos, SEEK_SET);
  for(i = 0; i < num_boards; i++)
  {
    cur_board = mzx_world->board_list[i];
    // Look at the name, width, and height of the just loaded board
    if(cur_board)
    {
      fread(cur_board->board_name, BOARD_NAME_SIZE, 1, fp);

      // Also patch a pointer to the global robot
      if(cur_board->robot_list)
        (mzx_world->board_list[i])->robot_list[0] = mzx_world->global_robot;

      // Also optimize out null objects
      retrieve_board_from_extram(mzx_world->board_list[i]);
      optimize_null_objects(mzx_world->board_list[i]);
      store_board_to_extram(mzx_world->board_list[i]);
    }
    else
    {
      fseek(fp, BOARD_NAME_SIZE, SEEK_CUR);
    }
  }

  meter_update_screen(mzx_world, &meter_curr, meter_target);

  // This pointer is now invalid. Clear it before we try to
  // send it back to extra RAM.
  mzx_world->current_board = NULL;
  set_current_board_ext(mzx_world,
   mzx_world->board_list[mzx_world->current_board_id]);

  mzx_world->active = 1;

  // Remove any null boards
  optimize_null_boards(mzx_world);

  // Resize this array if necessary
  set_update_done(mzx_world);

  // Find the player
  find_player(mzx_world);

  meter_restore_screen(mzx_world);

exit_ok_close:
  ret = 0;
exit_close:
  fclose(fp);
exit_out:
  return ret;

exit_recurse:
  ret = load_world(mzx_world, file, savegame, faded);
  goto exit_out;
}

// After clearing the above, use this to get default values. Use
// for loading of worlds (as opposed to save games).

__editor_maybe_static void default_global_data(World *mzx_world)
{
  int i;

  // Allocate space for sprites and give them default values (all 0's)
  mzx_world->num_sprites = MAX_SPRITES;
  mzx_world->sprite_list = calloc(MAX_SPRITES, sizeof(Sprite *));

  for(i = 0; i < 256; i++)
  {
    mzx_world->sprite_list[i] = malloc(sizeof(Sprite));
    memset(mzx_world->sprite_list[i], 0, sizeof(Sprite));
  }

  mzx_world->collision_list = calloc(MAX_SPRITES, sizeof(int));
  mzx_world->sprite_num = 0;

  // Set some default counter values
  // The others have to be here so their gateway functions will stick
  set_counter(mzx_world, "LIVES", mzx_world->starting_lives, 0);
  set_counter(mzx_world, "HEALTH", mzx_world->starting_health, 0);
  set_counter(mzx_world, "INVINCO", 0, 0);
  set_counter(mzx_world, "GEMS", 0, 0);
  set_counter(mzx_world, "HIBOMBS", 0, 0);
  set_counter(mzx_world, "LOBOMBS", 0, 0);
  set_counter(mzx_world, "COINS", 0, 0);
  set_counter(mzx_world, "TIME", 0, 0);
  set_counter(mzx_world, "ENTER_MENU", 1, 0);
  set_counter(mzx_world, "HELP_MENU", 1, 0);
  set_counter(mzx_world, "F2_MENU", 1, 0);
  set_counter(mzx_world, "LOAD_MENU", 1, 0);

  // Setup their gateways
  initialize_gateway_functions(mzx_world);

  mzx_world->multiplier = 10000;
  mzx_world->divider = 10000;
  mzx_world->c_divisions = 360;
  mzx_world->bi_mesg_status = 1;

  // And vlayer
  // Allocate space for vlayer.
  mzx_world->vlayer_size = 0x8000;
  mzx_world->vlayer_width = 256;
  mzx_world->vlayer_height = 128;
  mzx_world->vlayer_chars = malloc(0x8000);
  mzx_world->vlayer_colors = malloc(0x8000);
  memset(mzx_world->vlayer_chars, 32, 0x8000);
  memset(mzx_world->vlayer_colors, 7, 0x8000);

  // Default values for global params
  memset(mzx_world->keys, NO_KEY, NUM_KEYS);
  mzx_world->mesg_edges = 1;
  mzx_world->real_mod_playing[0] = 0;

  mzx_world->score = 0;
  mzx_world->blind_dur = 0;
  mzx_world->firewalker_dur = 0;
  mzx_world->freeze_time_dur = 0;
  mzx_world->slow_time_dur = 0;
  mzx_world->wind_dur = 0;

  for(i = 0; i < 8; i++)
  {
    mzx_world->pl_saved_x[i] = 0;
  }

  for(i = 0; i < 8; i++)
  {
    mzx_world->pl_saved_y[i] = 0;
  }
  memset(mzx_world->pl_saved_board, 0, 8);
  mzx_world->saved_pl_color = 27;
  mzx_world->player_restart_x = 0;
  mzx_world->player_restart_y = 0;
  mzx_world->under_player_id = 0;
  mzx_world->under_player_color = 7;
  mzx_world->under_player_param = 0;

  mzx_world->commands = 40;

  default_scroll_values(mzx_world);

  scroll_color = 15;

  mzx_world->lock_speed = 0;
  mzx_world->mzx_speed = mzx_world->default_speed;

  mzx_world->input_file = NULL;
  mzx_world->output_file = NULL;

  mzx_world->target_where = TARGET_NONE;
}

int reload_world(World *mzx_world, const char *file, int *faded)
{
  int rval;

  if(mzx_world->active)
  {
    clear_world(mzx_world);
    clear_global_data(mzx_world);
  }

  // Always switch back to regular mode before loading the world,
  // because we want the world's intrinsic palette to be applied.
  set_screen_mode(0);
  smzx_palette_loaded(0);
  set_palette_intensity(100);

  rval = load_world(mzx_world, file, 0, faded);

  default_global_data(mzx_world);

  *faded = 0;
  return rval;
}

int reload_savegame(World *mzx_world, const char *file, int *faded)
{
  // Check this SAV is actually loadable
  int ret = load_world(NULL, file, 1, faded);
  if(ret)
    return ret;

  // It is, so wipe the old world
  if(mzx_world->active)
  {
    clear_world(mzx_world);
    clear_global_data(mzx_world);
  }

  // And load the new one
  return load_world(mzx_world, file, 1, faded);
}

int reload_swap(World *mzx_world, const char *file, int *faded)
{
  int load_return;

  if(mzx_world->active)
    clear_world(mzx_world);

  load_return = load_world(mzx_world, file, 0, faded);

  mzx_world->current_board_id = mzx_world->first_board;
  set_current_board_ext(mzx_world,
   mzx_world->board_list[mzx_world->current_board_id]);

  return load_return;
}

// This only clears boards, no global data. Useful for swap world,
// when you want to maintain counters and sprites and all that.

void clear_world(World *mzx_world)
{
  // Do this before loading, when there's a world

  int i;
  int num_boards = mzx_world->num_boards;
  Board **board_list = mzx_world->board_list;

  for(i = 0; i < num_boards; i++)
  {
    if(mzx_world->current_board_id != i)
      retrieve_board_from_extram(board_list[i]);
    clear_board(board_list[i]);
  }
  free(board_list);

  if(mzx_world->global_robot->program)
    free(mzx_world->global_robot->program);

  if(mzx_world->global_robot->stack)
    free(mzx_world->global_robot->stack);

  if(mzx_world->global_robot->label_list && mzx_world->global_robot->used)
    clear_label_cache(mzx_world->global_robot->label_list,
     mzx_world->global_robot->num_labels);

  if(mzx_world->input_file)
  {
    fclose(mzx_world->input_file);
    mzx_world->input_file = NULL;
  }

  if(mzx_world->output_file)
  {
    fclose(mzx_world->output_file);
    mzx_world->output_file = NULL;
  }

  mzx_world->active = 0;

  end_sample();
}

// This clears the rest of the stuff.

void clear_global_data(World *mzx_world)
{
  int i;
  int num_sprites = mzx_world->num_sprites;
  int num_counters = mzx_world->num_counters;
  int num_strings = mzx_world->num_strings;
  counter **counter_list = mzx_world->counter_list;
  mzx_string **string_list = mzx_world->string_list;
  Sprite **sprite_list = mzx_world->sprite_list;

  free(mzx_world->vlayer_chars);
  free(mzx_world->vlayer_colors);
  mzx_world->vlayer_chars = NULL;
  mzx_world->vlayer_colors = NULL;

  for(i = 0; i < num_counters; i++)
  {
    free(counter_list[i]);
  }
  free(counter_list);
  mzx_world->counter_list = NULL;

  mzx_world->num_counters = 0;
  mzx_world->num_counters_allocated = 0;

  for(i = 0; i < num_strings; i++)
  {
    free(string_list[i]);
  }

  free(string_list);
  mzx_world->string_list = NULL;
  mzx_world->num_strings = 0;
  mzx_world->num_strings_allocated = 0;

  for(i = 0; i < num_sprites; i++)
  {
    free(sprite_list[i]);
  }

  free(sprite_list);
  mzx_world->sprite_list = NULL;
  mzx_world->num_sprites = 0;

  free(mzx_world->collision_list);
  mzx_world->collision_list = NULL;
  mzx_world->collision_count = 0;

  mzx_world->vlayer_size = 0;
  mzx_world->vlayer_width = 0;
  mzx_world->vlayer_height = 0;

  mzx_world->output_file_name[0] = 0;
  mzx_world->input_file_name[0] = 0;

  memset(mzx_world->custom_sfx, 0, NUM_SFX * 69);

  mzx_world->bomb_type = 1;
  mzx_world->dead = 0;
}

void default_scroll_values(World *mzx_world)
{
  mzx_world->scroll_base_color = 143;
  mzx_world->scroll_corner_color = 135;
  mzx_world->scroll_pointer_color = 128;
  mzx_world->scroll_title_color = 143;
  mzx_world->scroll_arrow_color = 142;
}

// FIXME: This function should probably die. It's unsafe.
void add_ext(char *src, const char *ext)
{
  int len = strlen(src);

  if((len < 4) || ((src[len - 4] != '.') && (src[len - 3] != '.')
   && (src[len - 2] != '.')))
  {
    strncat(src, ext, 4);
  }
}
