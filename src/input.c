/* Copyright (c) 2021-2024
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
#
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
#
* You should have received a copy of the GNU General Public
* License along with this program; if not, write to the
* Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
* Boston, MA 02110-1301 USA
#
* Authored by: Kris Henriksen <krishenriksen.work@gmail.com>
#
* AnberPorts-Keyboard-Mouse
*
* Part of the code is from from https://github.com/krishenriksen/AnberPorts/blob/master/AnberPorts-Keyboard-Mouse/main.c (mostly the fake keyboard)
* Fake Xbox code from: https://github.com/Emanem/js2xbox
*
* Modified (badly) by: Shanti Gilbert for EmuELEC
* Modified further by: Nikolai Wuttke for EmuELEC (Added support for SDL and the SDLGameControllerdb.txt)
* Modified further by: Jacob Smith
*
* Any help improving this code would be greatly appreciated!
*
* DONE: Xbox360 mode: Fix triggers so that they report from 0 to 255 like real Xbox triggers
*       Xbox360 mode: Figure out why the axis are not correctly labeled?  SDL_CONTROLLER_AXIS_RIGHTX / SDL_CONTROLLER_AXIS_RIGHTY / SDL_CONTROLLER_AXIS_TRIGGERLEFT / SDL_CONTROLLER_AXIS_TRIGGERRIGHT
*       Keyboard mode: Add a config file option to load mappings from.
*       add L2/R2 triggers
*
*/

#include <math.h>
#include "gptokeyb2.h"


/*

[config]
wordset = "level_select" "01" "02" "03" "04" "05" "06" "07" "08" "09" "10"

[controls:text_input]
overlay = clear

charset = "basic"

up    = prev_letter
down  = next_letter

right = add_letter
left  = remove_letter

a     = add_letter
b     = remove_letter

l1    = prev_letter10
r1    = next_letter10

start = finish_text
back  = cancel_text

[constrols:level_select]
wordset = "level_select"

up    = next_word
down  = prev_word

a = finish_text
*/


typedef struct {
    short keycode;
    bool  shift;
} character_map;


#define MAX_INPUT_LENGTH 128
#define MAX_TEXT_LENGTH  64
#define WORDS_SIZE_DEFAULT 16

bool char_set_builtin_mode = true;
char_set *root_char_set=NULL;
word_set *root_word_set=NULL;

character_map characters[256];

const char_set *active_char_set;
const word_set *active_word_set;

#define INPUT_MAGIC_CHAR 0xFFFF

static char input_text[MAX_TEXT_LENGTH] = {0};

static int current_letter = 0;
static size_t current_offset = 0;

static int current_word = 0;
static size_t current_word_len = 0;


void input_rem_char();
void input_add_char();
void input_dump_state();


void disable_input()
{
    active_char_set = NULL;
    active_word_set = NULL;
}


bool input_active()
{
    return (active_char_set != NULL || active_word_set != NULL);
}


char_set *_find_char_set(const char *name)
{
    char_set *curr_char_set = root_char_set;

    while (curr_char_set != NULL)
    {
        if (strcasecmp(name, curr_char_set->name) == 0)
        {
            return curr_char_set;
        }

        curr_char_set = curr_char_set->next;
    }

    return NULL;
}


const char_set *find_char_set(const char *name)
{
    char_set *curr_char_set = _find_char_set(name);

    if (curr_char_set == NULL)
        return root_char_set;

    return curr_char_set;
}


word_set *_find_word_set(const char *name)
{
    word_set *curr_word_set = root_word_set;
    word_set *prev_word_set = NULL;

    while (curr_word_set != NULL)
    {
        if (strcasecmp(name, curr_word_set->name) == 0)
        {
            if (prev_word_set != NULL)
            {   // unlink it
                prev_word_set->next = curr_word_set->next;
                curr_word_set->next = NULL;
            }

            if (curr_word_set != root_word_set)
            {   // move current wordset to the head of the root set
                curr_word_set->next = root_word_set;
                root_word_set = curr_word_set;
            }

            return curr_word_set;
        }

        prev_word_set = curr_word_set;
        curr_word_set = curr_word_set->next;
    }

    return NULL;
}


const word_set *find_word_set(const char *name)
{
    return _find_word_set(name);
}


void input_init()
{
    // Tag these as builtin charsets, if you override them they will be tagged as custom.
    // This helps the dumping code.
    char_set_builtin_mode = true;

    register_char_set("basic",    "ABCDEFGHIJKLMNOPQRSTUVWXYZ .,-_() 0123456789 ");
    register_char_set("extended", "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz .,-_() 0123456789 ");
    register_char_set("full",     "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz .,-_()@#%&*-+!\"\':;/?~`|{}$^=""[]\\<> 0123456789");
    register_char_set("digits",   "0123456789");
    register_char_set("hex",      "0123456789ABCDEF");
    register_char_set("alpha",    "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    // 8pen layout from 8VIM English layout:
    // Groups ordered: R-BOT, R-TOP, T-RIGHT, T-LEFT, L-TOP, L-BOT, B-LEFT, B-RIGHT
    register_char_set("8pen",    "nmf!arx?ybpqsdg'tcz.ihj,elk@ouvw");

    char_set_builtin_mode = false;

    const char_set *full_set = find_char_set("full");

    size_t character_len = strlen(full_set->characters);
    const keyboard_values *keyinfo;
    memset((void*)characters, '\0', sizeof(characters));
    char find_buff[3] = {0};

    for (size_t i = 0; i < character_len; i++)
    {
        find_buff[0] = full_set->characters[i];
        keyinfo = find_keyboard(find_buff);

        if (keyinfo == NULL)
        {
            printf("bad character '%c'\n", full_set->characters[i]);
            continue;
        }

        characters[(unsigned char)full_set->characters[i]].keycode = keyinfo->keycode;
        characters[(unsigned char)full_set->characters[i]].shift   = (keyinfo->modifier & MOD_SHIFT) != 0;
    }

    for (int i=0; i < MAX_TEXT_LENGTH; i++)
        input_text[i] = 0;
}


void dump_char_sets()
{
    char_set *curr_char_set = root_char_set;

    while (curr_char_set != NULL)
    {
        if (curr_char_set->builtin)
        {
            curr_char_set = curr_char_set->next;

            continue;
        }

        printf("charset = \"%s\"", curr_char_set->name);

        if (strchr(curr_char_set->characters, '"') == NULL)
            printf(" \"%s\"\n", curr_char_set->characters);

        else if (strchr(curr_char_set->characters, '\'') == NULL)
            printf(" '%s'\n", curr_char_set->characters);

        else
            printf(" \"%s\"\n", curr_char_set->characters);

        curr_char_set = curr_char_set->next;
    }
}


void dump_word_sets()
{
    word_set *curr_word_set = root_word_set;

    while (curr_word_set != NULL)
    {
        printf("wordset = \"%s\"", curr_word_set->name);

        for (size_t i=0; i < curr_word_set->words_len; i++)
        {
            if (strchr(curr_word_set->words[i], '"') == NULL)
                printf(" \"%s\"", curr_word_set->words[i]);

            else if (strchr(curr_word_set->words[i], '\'') == NULL)
                printf(" '%s'", curr_word_set->words[i]);

            else
                printf(" \"%s\"", curr_word_set->words[i]);
        }

        printf("\n");
        curr_word_set = curr_word_set->next;
    }
}


void register_char_set(const char *name, const char *characters)
{
    bool char_set_created = false;
    char_set *new_char_set = _find_char_set(name);

    if (new_char_set == NULL)
    {
        char_set_created = true;
        new_char_set = (char_set*)gptk_malloc(sizeof(char_set));
    }

    new_char_set->name       = string_register(name);
    new_char_set->characters = string_register(characters);
    new_char_set->characters_len = strlen(characters);
    new_char_set->builtin    = char_set_builtin_mode;

    if (char_set_created)
    {
        if (root_char_set != NULL)
        {
            char_set *curr_char_set = root_char_set;
            while (curr_char_set->next != NULL)
                curr_char_set = curr_char_set->next;

            curr_char_set->next = new_char_set;
        }
        else
        {
            root_char_set = new_char_set;
        }
    }
}


void register_word_set(const char *name, const char *word)
{
    word_set *curr_word_set = _find_word_set(name);

    if (curr_word_set == NULL)
    {   // create a new one
        curr_word_set = (word_set*)gptk_malloc(sizeof(word_set));

        curr_word_set->name = string_register(name);
        curr_word_set->words = (const char **)gptk_malloc(sizeof(char*) * WORDS_SIZE_DEFAULT);
        curr_word_set->words_alloc = WORDS_SIZE_DEFAULT;

        curr_word_set->next  = root_word_set;
        root_word_set = curr_word_set;
    }

    if ((curr_word_set->words_len+1) >= curr_word_set->words_alloc)
    {   // resize the words buffer if we need to
        size_t new_size = curr_word_set->words_alloc + WORDS_SIZE_DEFAULT;

        curr_word_set->words = (const char **)gptk_realloc(
            curr_word_set->words,
            sizeof(char*) * (curr_word_set->words_alloc),
            sizeof(char*) * new_size);

        curr_word_set->words_alloc = new_size;
    }

    // printf("register_word_set(%s, %s)\n", name, word);

    curr_word_set->words[curr_word_set->words_len++] = string_register(word);
}


void input_quit()
{
    char_set *curr_char_set = root_char_set;
    char_set *next_char_set;

    while (curr_char_set != NULL)
    {
        next_char_set = curr_char_set->next;

        free(curr_char_set);

        curr_char_set = next_char_set;
    }

    root_char_set = NULL;

    word_set *curr_word_set = root_word_set;
    word_set *next_word_set;

    while (curr_word_set != NULL)
    {
        next_word_set = curr_word_set->next;

        free(curr_word_set->words);
        free(curr_word_set);

        curr_word_set = next_word_set;
    }

    root_word_set = NULL;
}


void input_load_char_set(const char *name)
{   // load a char set, deactivate any word sets.
    if (!input_active())
    {   // Clear buffers, setup for new input session.
        for (int i=0; i < MAX_TEXT_LENGTH; i++)
            input_text[i] = 0;

        current_offset = 0;
        current_letter = 0;
        current_word = 0;
        current_word_len = 0;
    }

    active_char_set = find_char_set(name);

    if (active_char_set == NULL)
    {   // we always fallback to the basic charset.
        fprintf(stderr, "Unknown wordset \"%s\".\n", name);

        active_char_set = find_char_set("basic");
    }
    else
    {
        printf("Loaded char set \"%s\"\n", name);
    }

    // Don't type a preview character — the combo system or linear scroll
    // will display the first character when the user takes an action.
    // Just set up the internal state.
    if (input_text[current_offset] == 0)
    {
        current_letter = 0;
        input_text[current_offset] = active_char_set->characters[current_letter];
    }
    else
    {
        const char *temp = strchr(active_char_set->characters, input_text[current_offset]);

        if (temp == NULL)
            current_letter = 0;
        else
            current_letter = (int)(temp - active_char_set->characters);

        input_text[current_offset] = active_char_set->characters[current_letter];
    }

    // Clear the active word set
    active_word_set = NULL;
}


void input_load_word_set(const char *name)
{   // load a word set, deactivate any char sets.
    if (!input_active())
    {   // Clear buffers, setup for new input session.
        for (int i=0; i < MAX_TEXT_LENGTH; i++)
            input_text[i] = 0;

        current_offset = 0;
        current_letter = 0;
        current_word = 0;
        current_word_len = 0;
    }

    const word_set *temp_wordset = find_word_set(name);;

    if (temp_wordset == NULL)
    {
        fprintf(stderr, "Unknown wordset \"%s\".\n", name);
        return;
    }

    printf("Loaded word set \"%s\"\n", name);
    active_word_set = temp_wordset;
    active_char_set = NULL;

    if ((size_t)current_word >= active_word_set->words_len)
        current_word = 0;
}


void input_stop()
{
    if (active_word_set != NULL || active_char_set != NULL)
    {
        // Type a space when leaving text input mode (brief L2 press = space)
        emitTextInputKey(KEY_SPACE, false);
        haptic_bump(0x6FFF, 40);
        printf("Cleared input sets\n");
    }

    active_word_set = NULL;
    active_char_set = NULL;
}


void input_rem_char()
{
    emitTextInputKey(KEY_BACKSPACE, false);
}


void input_add_char()
{
    unsigned char c = (unsigned char)input_text[current_offset];
    short keycode = characters[c].keycode;
    bool shift    = characters[c].shift;

    if (keycode == 0)
    {
        // Fallback to '?' if the character is unknown
        emitTextInputKey(
            characters['?'].keycode,
            characters['?'].shift);
    }
    else
    {
        emitTextInputKey(
            keycode,
            shift);
    }
}

void input_dump_state()
{
    printf("\n");
    printf("input_text -- %s\n", input_text);
    printf("              ");

    for (size_t i=0; i < current_offset; i++)
    {
        printf(" ");
    }

    printf("^\n");
}


bool _input_add_letter(char letter)
{
    // Leave room for trailing '\0'
    if (current_offset >= MAX_TEXT_LENGTH - 1)
        return false;

    // 1. Write letter at current position
    input_text[current_offset] = letter;

    // 2. Emit that character
    input_add_char();

    // 3. Move cursor to "after last char"
    current_offset++;

    // 4. Maintain proper C-string termination
    input_text[current_offset] = 0;

    return true;
}


void _input_rem_letter()
{
    if (current_offset == 0)
        return;

    current_offset--;
    input_text[current_offset] = 0;

    input_rem_char();
}

void input_rem_word()
{
    while (current_word_len > 0)
    {
        _input_rem_letter();
        current_word_len -= 1;
    }
}


void input_add_word(const char *word, size_t word_len)
{
    for (size_t i=0; i < word_len; i++)
    {
        if (!_input_add_letter(word[i]))
            break;

        current_word_len += 1;
    }
}


void input_clear_state()
{
    printf("input_clear_state\n");

    while (current_offset > 0)
    {
        _input_rem_letter();
    }
}


void input_accept()
{
    printf("input_accept\n");

    emitTextInputKey(KEY_ENTER, false);
    pop_state();
}


void input_cancel()
{
    printf("input_cancel\n");

    input_clear_state();
    pop_state();
}


void input_set_state(const char *buff, size_t buff_len)
{
    if (buff_len >= MAX_TEXT_LENGTH)
    {
        buff_len = MAX_TEXT_LENGTH - 1; // Ensure we do not exceed buffer length
    }

    current_offset = 0;
    for (size_t i=0; i < buff_len; i++)
    {
        _input_add_letter(buff[i]);
    }
}


void input_get_state(char *buff, size_t buff_len)
{
    if (buff_len > 0)
    {
        if (buff_len > current_offset + 1)
        {
            buff_len = current_offset + 1;
        }

        strncpy(buff, input_text, buff_len);
    }
}


void input_add_letter()
{
    printf("input_add_letter\n");

    if (active_char_set != NULL && current_offset < MAX_TEXT_LENGTH)
    {
        current_offset++;

        if (input_text[current_offset] != 0)
        {
            const char *temp = strchr(active_char_set->characters, input_text[current_offset]);

            if (temp != NULL)
                current_letter = (int)(temp - active_char_set->characters);
            else
                current_letter = 0;
        }

        input_text[current_offset] = active_char_set->characters[current_letter];

        input_add_char();

        input_dump_state();
    }
}


void input_lower_case()
{
    printf("lower_case ");
    if (active_char_set != NULL && isupper(input_text[current_offset]))
    {
        char lower = tolower(input_text[current_offset]);

        const char *temp = strchr(active_char_set->characters, lower);

        if (temp != NULL)
        {
            current_letter = (int)(temp - active_char_set->characters);
            input_text[current_offset] = lower;

            input_rem_char();
            input_add_char();
        }

        printf("- lower\n");
    }
    else
        printf("- none\n");

    input_dump_state();
}


void input_upper_case()
{
    printf("upper_case ");

    if (active_char_set != NULL && islower(input_text[current_offset]))
    {
        char lower = toupper(input_text[current_offset]);

        const char *temp = strchr(active_char_set->characters, lower);

        if (temp != NULL)
        {
            current_letter = (int)(temp - active_char_set->characters);
            input_text[current_offset] = lower;

            input_rem_char();
            input_add_char();
        }

        printf("- lower\n");
    }
    else
        printf("- none\n");

    input_dump_state();
}


void input_toggle_case()
{
    printf("toggle_case - ");

    if (islower(input_text[current_offset]))
        input_upper_case();

    else if (isupper(input_text[current_offset]))
        input_lower_case();

    else
        printf("none\n");
}


void input_rem_letter()
{
    printf("input_rem_letter");

    if (current_offset > 0)
    {
        const char *temp;

        input_rem_char();

        current_offset--;

        if (active_char_set != NULL && input_text[current_offset] != 0)
        {
            temp = strchr(active_char_set->characters, input_text[current_offset]);

            if (temp != NULL)
                current_letter = (int)(temp - active_char_set->characters);

            else
                current_letter = 0;
        }
        else
            current_letter = 0;

        printf(" - %d\n", current_letter);

        if (input_text[current_offset] != active_char_set->characters[current_letter])
        {
            input_rem_char();
            input_text[current_offset] = active_char_set->characters[current_letter];
            input_add_char();
        }
    }
    else
        printf("\n");

    input_dump_state();
}


void input_next_letter(int amount)
{
    printf("input_next_letter(%d)\n", amount);
    if (active_char_set)
    {
        current_letter = (active_char_set->characters_len + (current_letter + amount)) % active_char_set->characters_len;

        if (current_letter < 0)
            current_letter += active_char_set->characters_len;

        input_rem_char();
        input_text[current_offset] = active_char_set->characters[current_letter];
        input_add_char();

        input_dump_state();
    }
}


void input_prev_letter(int amount)
{
    printf("input_prev_letter(%d)\n", amount);
    if (active_char_set)
    {
        current_letter = (active_char_set->characters_len + (current_letter - amount)) % active_char_set->characters_len;

        if (current_letter < 0)
            current_letter += active_char_set->characters_len;

        input_rem_char();
        input_text[current_offset] = active_char_set->characters[current_letter];
        input_add_char();

        input_dump_state();
    }
}


void input_next_word(int amount)
{
    if (active_word_set)
    {
        current_word = (active_word_set->words_len + (current_word + amount)) % active_word_set->words_len;

        input_rem_word();
        input_add_word(active_word_set->words[current_word], strlen(active_word_set->words[current_word]));

        input_dump_state();
    }
}


void input_prev_word(int amount)
{
    if (active_word_set)
    {
        current_word = (active_word_set->words_len + (current_word - amount)) % active_word_set->words_len;

        input_rem_word();
        input_add_word(active_word_set->words[current_word], strlen(active_word_set->words[current_word]));

        input_dump_state();
    }
}


// ---------------------------------------------------------------------------
// Fighting-game-style direction combo character selection
// ---------------------------------------------------------------------------
//
// Press two D-pad directions in sequence (within COMBO_TIMEOUT_MS) to
// select one of 8 character groups, then press A/B/X/Y to pick the
// letter slot within that group.
//
// Combo table (2nd direction determines which of the 2 parts in that quadrant):
//   →↓ (RIGHT → DOWN)  = group 0 = nmf!     →↑ (RIGHT → UP)      = group 1 = arx?
//   ↑→ (UP → RIGHT)    = group 2 = ybpq      ↑← (UP → LEFT)      = group 3 = sdg'
//   ←↑ (LEFT → UP)     = group 4 = tcz.      ←↓ (LEFT → DOWN)    = group 5 = ihj,
//   ↓← (DOWN → LEFT)   = group 6 = elk@      ↓→ (DOWN → RIGHT)   = group 7 = ouvw
//
// The 8 groups each have up to 4 characters, selected by slots 0-3.

#define COMBO_TIMEOUT_MS 600

// Last D-pad direction pressed (for combo detection)
static int combo_first_dir = -1;   // 0=RIGHT, 1=UP, 2=LEFT, 3=DOWN, -1=none
static Uint32 combo_first_time = 0;

// Current group state
static int dir_group_index = -1;   // 0..7 or -1 = unset
static int dir_group_base = 0;     // start index in active charset for this group
static int dir_group_size = 4;     // how many slots in current group
static int dir_current_slot = 0;   // 0..3 within current group


// Compute dir_group_base/size from group_index
static void dir_compute_group()
{
    if (active_char_set == NULL || active_char_set->characters_len == 0)
        return;

    int total = (int)active_char_set->characters_len;
    int groups = 8;
    int per_group = (total + groups - 1) / groups;
    if (per_group > 4) per_group = 4;

    if (dir_group_index < 0) dir_group_index = 0;
    if (dir_group_index >= groups) dir_group_index = groups - 1;

    dir_group_base = dir_group_index * per_group;
    if (dir_group_base >= total)
        dir_group_base = (groups - 1) * per_group;
    if (dir_group_base >= total)
        dir_group_base = total - per_group;

    dir_group_size = total - dir_group_base;
    if (dir_group_size > per_group) dir_group_size = per_group;
    if (dir_group_size < 0) dir_group_size = 0;
}


// Update the internal buffer to the current slot character.
// Does NOT type anything — typing happens only on commit.
static void dir_show_slot()
{
    dir_compute_group();
    if (dir_group_size <= 0)
        return;

    int slot = dir_current_slot;
    if (slot < 0) slot = 0;
    if (slot >= dir_group_size) slot = dir_group_size - 1;

    int char_idx = dir_group_base + slot;
    if (char_idx >= (int)active_char_set->characters_len)
        char_idx = (int)active_char_set->characters_len - 1;

    // Just update the internal buffer — no key events
    input_text[current_offset] = active_char_set->characters[char_idx];
}


// Analog stick deadzone for direction detection
#define DIR_STICK_DEADZONE 12000
// Diagonal ratio: one axis must be this many times stronger than the other
// to register as a cardinal direction. Prevents accidental diagonals.
#define DIR_DIAGONAL_RATIO 2.0f

// Track last stick direction for edge detection
static int dir_stick_last_dir = -1;  // -1 = centered, 0-3 = direction
// Debounce time: ignore direction changes within this many ms


// Determine which cardinal direction the stick is pointing, with diagonal
// exclusion zone so we only trigger on clean cardinal inputs.
static int dir_get_stick_dir(int ax, int ay)
{
    int abs_x = abs(ax);
    int abs_y = abs(ay);

    // Must be outside the center deadzone
    if (abs_x < DIR_STICK_DEADZONE && abs_y < DIR_STICK_DEADZONE)
        return -1;

    // Diagonal exclusion: one axis must dominate by at least DIR_DIAGONAL_RATIO
    // Unless one axis is very strongly pushed (>22000), in which case
    // we trust it as a clear cardinal intent.
    bool strong_axis = (abs_x > 22000 || abs_y > 22000);

    if (!strong_axis)
    {
        float ratio = (abs_x > abs_y) ? ((float)abs_x / abs_y) : ((float)abs_y / abs_x);
        if (ratio < DIR_DIAGONAL_RATIO)
        {
            // Too close to a diagonal — this is ambiguous.
            // Keep the last known direction (if any) to avoid flickering.
            return dir_stick_last_dir;
        }
    }

    // Clean cardinal direction
    if (abs_x > abs_y)
        return (ax > 0) ? 0 : 2;  // RIGHT or LEFT
    else
        return (ay < 0) ? 1 : 3;  // UP or DOWN
}


// Track which stick is currently "active" to prevent cross-talk.
// Both sticks share the same combo/commit state.
#define STICK_NONE 0
#define STICK_LEFT 1
#define STICK_RIGHT 2

// Adaptive scroll state
static Uint32 scroll_timer = 0;
static int stick_active = STICK_NONE;


// Call this from state_update() every frame to poll both analog sticks
// for direction combo detection. Either stick can be used for input.
void input_dir_analog_poll()
{
    if (!input_active() || active_char_set == NULL)
        return;

    int lx = current_state.current_left_analog_x;
    int ly = current_state.current_left_analog_y;
    int rx = current_state.current_right_analog_x;
    int ry = current_state.current_right_analog_y;
    int ax = 0, ay = 0;

    // Determine which stick has movement
    int left_mag  = abs(lx) + abs(ly);
    int right_mag = abs(rx) + abs(ry);

    int pick = STICK_NONE;

    if (stick_active == STICK_NONE)
    {
        // No stick active — pick the one with more movement
        if (left_mag > right_mag && left_mag > DIR_STICK_DEADZONE)
            pick = STICK_LEFT;
        else if (right_mag > left_mag && right_mag > DIR_STICK_DEADZONE)
            pick = STICK_RIGHT;
        else if (left_mag > DIR_STICK_DEADZONE)
            pick = STICK_LEFT;
        else if (right_mag > DIR_STICK_DEADZONE)
            pick = STICK_RIGHT;
        else
            return; // both centered, nothing to do
    }
    else if (stick_active == STICK_LEFT)
    {
        // If right stick is now stronger, switch to it
        if (right_mag > left_mag && right_mag > DIR_STICK_DEADZONE)
            pick = STICK_RIGHT;
        else
            pick = (left_mag > DIR_STICK_DEADZONE) ? STICK_LEFT : STICK_NONE;
    }
    else // STICK_RIGHT
    {
        // If left stick is now stronger, switch to it
        if (left_mag > right_mag && left_mag > DIR_STICK_DEADZONE)
            pick = STICK_LEFT;
        else
            pick = (right_mag > DIR_STICK_DEADZONE) ? STICK_RIGHT : STICK_NONE;
    }

    if (pick == STICK_NONE)
    {
        // Stick returned to center
        if (stick_active != STICK_NONE && dir_stick_last_dir >= 0)
        {
            // Commit if a group is selected
            if (dir_group_index >= 0)
                input_dir_commit_current();
            dir_stick_last_dir = -1;
        }
        stick_active = STICK_NONE;
        return;
    }

    // Read the active stick's values
    if (pick == STICK_LEFT)
    {
        ax = lx; ay = ly;
    }
    else
    {
        ax = rx; ay = ry;
    }
    stick_active = pick;

    // Determine current stick direction with diagonal exclusion
    int stick_dir = dir_get_stick_dir(ax, ay);

    // Edge detection: only fire when direction changes
    if (stick_dir != dir_stick_last_dir)
    {
        if (stick_dir >= 0)
        {
            // Haptic feedback for landing on a cardinal direction
            haptic_bump(0x2FFF, 15);

            // Treat this as a combo press
            input_dir_combo_press(stick_dir);
        }
        else
        {
            // Stick returned to center — commit if a group is selected
            if (dir_group_index >= 0)
                input_dir_commit_current();
        }
        dir_stick_last_dir = stick_dir;
    }
}



// After a combo selects a group, each additional direction change cycles
// to the next slot (slot+1 modulo 4). This matches 8pen where continuing
// the gesture motion past the group selects the specific letter.
// The first change goes to slot 1, second to slot 2, third to slot 3,
// fourth wraps to slot 0, etc.
// We ignore the actual direction and just count changes.


// Called when a direction is pressed (from state dispatch or analog poll).
// First two directions = combo to select group.
// Subsequent directions = refine slot within the current group.
void input_dir_combo_press(int direction)
{
    if (!input_active() || active_char_set == NULL)
        return;

    // direction: 0=RIGHT, 1=UP, 2=LEFT, 3=DOWN
    if (direction < 0 || direction > 3) return;

    Uint32 now = SDL_GetTicks();

    // If a group is already selected, each new direction cycles to the next slot
    if (dir_group_index >= 0)
    {
        // Don't count the direction the stick is already pointing in
        // Actually, every direction change counts as one cycle
        dir_current_slot = (dir_current_slot + 1) % 4;
        dir_show_slot();
        haptic_bump(0x3FFF, 20);
        return;
    }

    // If we have a pending first direction within timeout, try to match a combo
    if (combo_first_dir >= 0 && (now - combo_first_time) < COMBO_TIMEOUT_MS)
    {
        // Check all 8 combos: [first][second] = group_index
        // First direction selects the quadrant, second selects the part
        static const int combo_table[4][4] = {
            // second: RIGHT   UP     LEFT   DOWN
            /* first RIGHT */ { -1,   1,    -1,   0 },   // →→ invalid, →↑=1, →← invalid, →↓=0
            /* first UP    */ {  2,  -1,     3,  -1 },   // ↑→=2, ↑↑ invalid, ↑←=3, ↑↓ invalid
            /* first LEFT  */ { -1,   4,    -1,   5 },   // ←→ invalid, ←↑=4, ←← invalid, ←↓=5
            /* first DOWN  */ {  7,  -1,     6,  -1 },   // ↓→=7, ↓↑ invalid, ↓←=6, ↓↓ invalid
        };

        int group = combo_table[combo_first_dir][direction];
        if (group >= 0 && group < 8)
        {
            // Valid combo — select the group, start at slot 0
            dir_group_index = group;
            dir_current_slot = 0;
            combo_first_dir = -1;
            dir_show_slot();
            haptic_bump(0x5FFF, 30);
            return;
        }
        // Invalid combo (e.g., →→) — reset and start fresh
        combo_first_dir = -1;
    }

    // Start a new combo
    combo_first_dir = direction;
    combo_first_time = now;
}


// Commit a specific slot within the current group. Types the character
// and advances the cursor. Called from A/B/X/Y (slot 0-3).
void input_dir_commit_slot(int slot)
{
    if (!input_active() || active_char_set == NULL)
        return;

    // Cancel any pending combo
    combo_first_dir = -1;

    // If no group is selected yet, default to group 0
    if (dir_group_index < 0)
    {
        dir_group_index = 0;
        dir_compute_group();
    }

    if (slot < 0) slot = 0;
    if (slot > 3) slot = 3;

    // Compute character index
    dir_compute_group();

    int char_idx = dir_group_base + slot;
    if (char_idx < 0) char_idx = 0;
    if (dir_group_size > 0 && slot >= dir_group_size)
        char_idx = dir_group_base + (dir_group_size - 1); // clamp to last valid slot
    if (char_idx >= (int)active_char_set->characters_len)
        char_idx = (int)active_char_set->characters_len - 1;

    // Type and advance (face button commit — no preview involved)
    input_text[current_offset] = active_char_set->characters[char_idx];
    input_add_char();

    current_offset++;
    if ((size_t)current_offset >= MAX_TEXT_LENGTH)
        current_offset = MAX_TEXT_LENGTH - 1;
    input_text[current_offset] = 0;

    // Reset combo state for next character
    // Keep group selected so repeated A presses type consecutive chars
    // from the same group.

    haptic_bump(0x7FFF, 50);
}


// Commit the currently-selected slot and advance cursor.
// This is called when the analog stick returns to center after a combo.
void input_dir_commit_current()
{
    if (!input_active() || active_char_set == NULL)
        return;

    if (dir_group_index < 0)
        return;  // no group selected, nothing to commit

    // Type the character now (no preview was typed during the combo)
    char c = input_text[current_offset];
    if (c != 0)
        input_add_char();
    current_offset++;
    if ((size_t)current_offset >= MAX_TEXT_LENGTH)
        current_offset = MAX_TEXT_LENGTH - 1;
    input_text[current_offset] = 0;

    // Reset for next character
    dir_group_index = -1;
    dir_current_slot = 0;

    haptic_bump(0x7FFF, 50);
}


void input_dir_reset_position()
{
    combo_first_dir = -1;
    combo_first_time = 0;
    dir_group_index = -1;
    dir_current_slot = 0;
    dir_group_base = 0;
    dir_group_size = 4;
    dir_stick_last_dir = -1;
    stick_active = STICK_NONE;
    scroll_timer = 0;
    // printf("dir_reset_position\n");
}


// ---------------------------------------------------------------------------
// Adaptive scroll acceleration
// ---------------------------------------------------------------------------
// Reads the right analog Y value and emits scroll events with speed
// proportional to how far the stick is pushed.

#define SCROLL_DEADZONE 5000
#define SCROLL_MAX_RATE 20    // ms between scrolls at full tilt
#define SCROLL_MIN_RATE 150   // ms between scrolls at minimum push

void scroll_adaptive_poll()
{
    // Don't scroll while in text input mode (right stick used for combos)
    if (input_active())
        return;

    int ay = current_state.current_right_analog_y;
    int abs_y = abs(ay);

    // Deadzone check
    if (abs_y < SCROLL_DEADZONE)
    {
        scroll_timer = 0;
        return;
    }

    // Calculate scroll rate based on stick magnitude
    // Map stick magnitude (SCROLL_DEADZONE..32768) to rate (SCROLL_MIN_RATE..SCROLL_MAX_RATE)
    float t = (float)(abs_y - SCROLL_DEADZONE) / (32768.0f - SCROLL_DEADZONE);
    if (t > 1.0f) t = 1.0f;

    // Apply acceleration power curve
    float power = (float)current_state.scroll_accel_power / 100.0f;
    if (power < 0.1f) power = 1.0f;
    t = powf(t, 1.0f / power);  // inverse: higher power = more acceleration

    int rate = (int)(SCROLL_MIN_RATE - (SCROLL_MIN_RATE - SCROLL_MAX_RATE) * t);
    if (rate < SCROLL_MAX_RATE) rate = SCROLL_MAX_RATE;
    if (rate > SCROLL_MIN_RATE) rate = SCROLL_MIN_RATE;

    // Check if enough time has passed since last scroll
    Uint32 now = SDL_GetTicks();
    if (scroll_timer != 0 && (now - scroll_timer) < (Uint32)rate)
        return;

    scroll_timer = now;

    // Emit scroll event (inverted: stick UP = scroll down, stick DOWN = scroll up)
    int direction = (ay < 0) ? 1 : -1;
    emitMouseWheel(direction);
}
