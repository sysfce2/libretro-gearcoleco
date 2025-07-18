/*
 * Gearcoleco - ColecoVision Emulator
 * Copyright (C) 2021  Ignacio Sanchez

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#include <math.h>
#include "imgui/imgui.h"
#include "imgui/fonts/RobotoMedium.h"
#include "imgui/keyboard.h"
#include "nfd/nfd.h"
#include "nfd/nfd_sdl2.h"
#include "config.h"
#include "emu.h"
#include "../../src/gearcoleco.h"
#include "renderer.h"
#include "application.h"
#include "license.h"
#include "backers.h"
#include "gui_debug.h"
#include "gui_debug_memory.h"
#include "gui_debug_constants.h"

#define GUI_IMPORT
#include "gui.h"

static int main_menu_height;
static bool dialog_in_use = false;
static SDL_Scancode* configured_key;
static int* configured_button;
static ImVec4 custom_palette[16];
static bool shortcut_open_rom = false;
static ImFont* default_font[4];
static char bios_path[4096] = "";
static char savefiles_path[4096] = "";
static char savestates_path[4096] = "";
static int main_window_width = 0;
static int main_window_height = 0;
static bool status_message_active = false;
static char status_message[4096] = "";
static u32 status_message_start_time = 0;
static u32 status_message_duration = 0;

static void main_menu(void);
static void main_window(void);
static void file_dialog_open_rom(void);
static void file_dialog_load_ram(void);
static void file_dialog_save_ram(void);
static void file_dialog_load_state(void);
static void file_dialog_save_state(void);
static void file_dialog_choose_savestate_path(void);
static void file_dialog_load_bios(void);
static void file_dialog_load_symbols(void);
static void file_dialog_save_screenshot(void);
static void file_dialog_set_native_window(SDL_Window* window, nfdwindowhandle_t* native_window);
static void keyboard_configuration_item(const char* text, SDL_Scancode* key, int player);
static void gamepad_configuration_item(const char* text, int* button, int player);
static void popup_modal_keyboard();
static void popup_modal_gamepad(int pad);
static void popup_modal_about(void);
static void popup_modal_bios(void);
static GC_Color color_float_to_int(ImVec4 color);
static ImVec4 color_int_to_float(GC_Color color);
static void update_palette(void);
static void push_recent_rom(std::string path);
static void menu_reset(void);
static void menu_pause(void);
static void menu_ffwd(void);
static void show_info(void);
static void show_fps(void);
static void show_status_message(void);
static Cartridge::CartridgeRegions get_region(int index);
static void call_save_screenshot(const char* path);
static void set_style(void);
static ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t);

void gui_init(void)
{
    if (NFD_Init() != NFD_OKAY)
    {
        Log("NFD Error: %s", NFD_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingWithShift = true;
    io.IniFilename = config_imgui_file_path;
    io.FontGlobalScale /= application_display_scale;

#if defined(__APPLE__) || defined(_WIN32)
    if (config_debug.multi_viewport)
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif

    gui_roboto_font = io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, 17.0f * application_display_scale, NULL, io.Fonts->GetGlyphRangesCyrillic());

    ImFontConfig font_cfg;

    for (int i = 0; i < 4; i++)
    {
        font_cfg.SizePixels = (13.0f + (i * 3)) * application_display_scale;
        default_font[i] = io.Fonts->AddFontDefault(&font_cfg);
    }

    gui_default_font = default_font[config_debug.font_size];

    update_palette();
    set_style();

    emu_audio_mute(!config_audio.enable);

    strcpy(bios_path, config_emulator.bios_path.c_str());
    strcpy(savefiles_path, config_emulator.savefiles_path.c_str());
    strcpy(savestates_path, config_emulator.savestates_path.c_str());

    if (strlen(bios_path) > 0)
        emu_load_bios(bios_path);

    emu_set_overscan(config_debug.debug ? 0 : config_video.overscan);
    emu_video_no_sprite_limit(config_video.sprite_limit);

    gui_debug_memory_init();
}

void gui_destroy(void)
{
    ImGui::DestroyContext();
    NFD_Quit();
}

void gui_render(void)
{
    ImGui::NewFrame();

    if (config_debug.debug)
        ImGui::DockSpaceOverViewport();

    gui_in_use = dialog_in_use;
    
    main_menu();

    gui_main_window_hovered = false;

    if((!config_debug.debug && !emu_is_empty()) || (config_debug.debug && config_debug.show_screen))
        main_window();

    gui_debug_windows();

    if (config_emulator.show_info)
        show_info();

    show_status_message();

    ImGui::Render();
}

void gui_shortcut(gui_ShortCutEvent event)
{
    switch (event)
    {  
    case gui_ShortcutOpenROM:
        shortcut_open_rom = true;
        break;
    case gui_ShortcutReset:
        menu_reset();
        break;
    case gui_ShortcutPause:
        menu_pause();
        break;
    case gui_ShortcutFFWD:
        config_emulator.ffwd = !config_emulator.ffwd;
        menu_ffwd();
        break;
    case gui_ShortcutSaveState:
    {
        std::string message("Saving state to slot ");
        message += std::to_string(config_emulator.save_slot + 1);
        gui_set_status_message(message.c_str(), 3000);
        emu_save_state_slot(config_emulator.save_slot + 1);
        break;
    }
    case gui_ShortcutLoadState:
    {
        std::string message("Loading state from slot ");
        message += std::to_string(config_emulator.save_slot + 1);
        gui_set_status_message(message.c_str(), 3000);
        emu_load_state_slot(config_emulator.save_slot + 1);
        break;
    }
    case gui_ShortcutScreenshot:
        call_save_screenshot(NULL);
        break;
    case gui_ShortcutDebugStep:
        if (config_debug.debug)
            emu_debug_step();
        break;
    case gui_ShortcutDebugContinue:
        if (config_debug.debug)
            emu_debug_continue();
        break;
    case gui_ShortcutDebugNextFrame:
        if (config_debug.debug)
        {
            emu_debug_next_frame();
            gui_debug_memory_step_frame();
        }
        break;
    case gui_ShortcutDebugBreakpoint:
        if (config_debug.debug)
            gui_debug_toggle_breakpoint();
        break;
    case gui_ShortcutDebugRuntocursor:
        if (config_debug.debug)
            gui_debug_runtocursor();
        break;
    case gui_ShortcutDebugGoBack:
        if (config_debug.debug)
            gui_debug_go_back();
        break;
    case gui_ShortcutDebugCopy:
        if (config_debug.debug)
            gui_debug_memory_copy();
        break;
    case gui_ShortcutDebugPaste:
        if (config_debug.debug)
            gui_debug_memory_paste();
        break;
    case gui_ShortcutShowMainMenu:
        config_emulator.show_menu = !config_emulator.show_menu;
        break;
    default:
        break;
    }
}

void gui_load_rom(const char* path)
{
    std::string message("Loading ROM ");
    message += path;
    gui_set_status_message(message.c_str(), 3000);

    Cartridge::ForceConfiguration config;

    config.region = get_region(config_emulator.region);
    config.type = Cartridge::CartridgeNotSupported;

    push_recent_rom(path);
    emu_resume();
    emu_load_rom(path, config);

    gui_debug_reset();

    std::string str(path);
    str = str.substr(0, str.find_last_of("."));
    str += ".sym";
    gui_debug_load_symbols_file(str.c_str());

    if (config_emulator.start_paused)
    {
        emu_pause();
        
        for (int i=0; i < (GC_RESOLUTION_WIDTH_WITH_OVERSCAN * GC_RESOLUTION_HEIGHT_WITH_OVERSCAN); i++)
        {
            emu_frame_buffer[i] = 0;
        }
    }

    if (!emu_is_empty())
    {
        char title[256];
        snprintf(title, 256, "%s %s - %s", GEARCOLECO_TITLE, GEARCOLECO_VERSION, emu_get_core()->GetCartridge()->GetFileName());
        application_update_title(title);
    }
}

void gui_set_status_message(const char* message, u32 milliseconds)
{
    if (config_emulator.status_messages)
    {
        strcpy(status_message, message);
        status_message_active = true;
        status_message_start_time = SDL_GetTicks();
        status_message_duration = milliseconds;
    }
}

static void main_menu(void)
{
    bool open_rom = false;
    bool open_ram = false;
    bool save_ram = false;
    bool open_state = false;
    bool save_state = false;
    bool open_about = false;
    bool open_symbols = false;
    bool save_screenshot = false;
    bool choose_savestates_path = false;
    bool open_bios = false;
    bool open_bios_warning = false;

    for (int i = 0; i < 16; i++)
        custom_palette[i] = color_int_to_float(config_video.color[i]);

    gui_main_menu_hovered = false;

    if (config_emulator.show_menu && ImGui::BeginMainMenuBar())
    {
        gui_main_menu_hovered = ImGui::IsWindowHovered();

        if (ImGui::BeginMenu(GEARCOLECO_TITLE))
        {
            gui_in_use = true;

            if (ImGui::MenuItem("Open ROM...", "Ctrl+O"))
            {
                if (emu_is_bios_loaded())
                    open_rom = true;
                else
                    open_bios_warning = true;
            }

            if (ImGui::BeginMenu("Open Recent"))
            {
                for (int i = 0; i < config_max_recent_roms; i++)
                {
                    if (config_emulator.recent_roms[i].length() > 0)
                    {
                        if (ImGui::MenuItem(config_emulator.recent_roms[i].c_str()))
                        {
                            if (emu_is_bios_loaded())
                            {
                                char rom_path[4096];
                                strcpy(rom_path, config_emulator.recent_roms[i].c_str());
                                gui_load_rom(rom_path);
                            }
                            else
                                open_bios_warning = true;
                        }
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::Separator();
            
            if (ImGui::MenuItem("Reset", "Ctrl+R"))
            {
                menu_reset();
            }

            if (ImGui::MenuItem("Pause", "Ctrl+P", &config_emulator.paused))
            {
                menu_pause();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Fast Forward", "Ctrl+F", &config_emulator.ffwd))
            {
                menu_ffwd();
            }

            if (ImGui::BeginMenu("Fast Forward Speed"))
            {
                ImGui::PushItemWidth(100.0f);
                ImGui::Combo("##fwd", &config_emulator.ffwd_speed, "X 1.5\0X 2\0X 2.5\0X 3\0Unlimited\0\0");
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save State As...")) 
            {
                save_state = true;
            }

            if (ImGui::MenuItem("Load State From..."))
            {
                open_state = true;
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("Save State Slot"))
            {
                ImGui::PushItemWidth(100.0f);
                ImGui::Combo("##slot", &config_emulator.save_slot, "Slot 1\0Slot 2\0Slot 3\0Slot 4\0Slot 5\0\0");
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Save State", "Ctrl+S")) 
            {
                std::string message("Saving state to slot ");
                message += std::to_string(config_emulator.save_slot + 1);
                gui_set_status_message(message.c_str(), 3000);
                emu_save_state_slot(config_emulator.save_slot + 1);
            }

            if (ImGui::MenuItem("Load State", "Ctrl+L"))
            {
                std::string message("Loading state from slot ");
                message += std::to_string(config_emulator.save_slot + 1);
                gui_set_status_message(message.c_str(), 3000);
                emu_load_state_slot(config_emulator.save_slot + 1);
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save Screenshot As..."))
            {
                save_screenshot = true;
            }

            if (ImGui::MenuItem("Save Screenshot", "Ctrl+X"))
            {
                call_save_screenshot(NULL);
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Quit", "Ctrl+Q"))
            {
                application_trigger_quit();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Emulator"))
        {
            gui_in_use = true;

            if (ImGui::BeginMenu("Refresh Rate"))
            {
                ImGui::PushItemWidth(130.0f);
                if (ImGui::Combo("##emu_rate", &config_emulator.region, "Auto\0NTSC (60 Hz)\0PAL (50 Hz)\0\0"))
                {
                    if (config_emulator.region > 0)
                    {
                        config_emulator.ffwd = false;
                        config_audio.sync = true;
                    }
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("BIOS"))
            {
                if (ImGui::MenuItem("Load BIOS..."))
                {
                    open_bios = true;
                }
                ImGui::PushItemWidth(350);
                if (ImGui::InputText("##bios_path", bios_path, IM_ARRAYSIZE(bios_path), ImGuiInputTextFlags_AutoSelectAll))
                {
                    config_emulator.bios_path.assign(bios_path);
                    emu_load_bios(bios_path);
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            ImGui::Separator();

            ImGui::MenuItem("Start Paused", "", &config_emulator.start_paused);
            
            ImGui::Separator();

            if (ImGui::BeginMenu("Save State Location"))
            {
                ImGui::PushItemWidth(220.0f);
                if (ImGui::Combo("##savestate_option", &config_emulator.savestates_dir_option, "Savestates In Custom Folder\0Savestates In ROM Folder\0\0"))
                {
                    emu_savestates_dir_option = config_emulator.savestates_dir_option;
                }

                if (config_emulator.savestates_dir_option == 0)
                {
                    if (ImGui::MenuItem("Choose Savestate Folder..."))
                    {
                        choose_savestates_path = true;
                    }

                    ImGui::PushItemWidth(350);
                    if (ImGui::InputText("##savestate_path", savestates_path, IM_ARRAYSIZE(savestates_path), ImGuiInputTextFlags_AutoSelectAll))
                    {
                        config_emulator.savestates_path.assign(savestates_path);
                        strcpy(emu_savestates_path, savestates_path);
                    }
                    ImGui::PopItemWidth();
                }

                ImGui::EndMenu();
            }

            ImGui::Separator();

            ImGui::MenuItem("Show ROM info", "", &config_emulator.show_info);

            ImGui::MenuItem("Status Messages", "", &config_emulator.status_messages);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Video"))
        {
            gui_in_use = true;

            if (ImGui::MenuItem("Full Screen", "F11", &config_emulator.fullscreen))
            {
                application_trigger_fullscreen(config_emulator.fullscreen);
            }

            ImGui::MenuItem("Show Menu", "CTRL+M", &config_emulator.show_menu);

            if (ImGui::MenuItem("Resize Window to Content"))
            {
                if (!config_debug.debug)
                    application_trigger_fit_to_content(main_window_width, main_window_height + main_menu_height);
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("Scale"))
            {
                ImGui::PushItemWidth(250.0f);
                ImGui::Combo("##scale", &config_video.scale, "Integer Scale (Auto)\0Integer Scale (Manual)\0Scale to Window Height\0Scale to Window Width & Height\0\0");
                if (config_video.scale == 1)
                    ImGui::SliderInt("##scale_manual", &config_video.scale_manual, 1, 10);
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Aspect Ratio"))
            {
                ImGui::PushItemWidth(160.0f);
                ImGui::Combo("##ratio", &config_video.ratio, "Square Pixels (1:1 PAR)\0Standard (4:3 DAR)\0Wide (16:9 DAR)\0Wide (16:10 DAR)\0\0");
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Overscan"))
            {
                ImGui::PushItemWidth(150.0f);
                if (ImGui::Combo("##overscan", &config_video.overscan, "Disabled\0Top+Bottom\0Full (284 width)\0Full (320 width)\0\0"))
                {
                    emu_set_overscan(config_debug.debug ? 0 : config_video.overscan);
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Vertical Sync", "", &config_video.sync))
            {
                SDL_GL_SetSwapInterval(config_video.sync ? 1 : 0);

                if (config_video.sync)
                {
                    config_audio.sync = true;
                    emu_audio_reset();
                }
            }

            ImGui::MenuItem("Show FPS", "", &config_video.fps);

            ImGui::Separator();

            ImGui::MenuItem("Bilinear Filtering", "", &config_video.bilinear);
            if (ImGui::MenuItem("Disable Sprite Limit", "", &config_video.sprite_limit))
            {
                emu_video_no_sprite_limit(config_video.sprite_limit);
            }

            if (ImGui::BeginMenu("Screen Ghosting"))
            {
                ImGui::MenuItem("Enable Screen Ghosting", "", &config_video.mix_frames);
                ImGui::SliderFloat("##screen_ghosting", &config_video.mix_frames_intensity, 0.0f, 1.0f, "Intensity = %.2f");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Scanlines"))
            {
                ImGui::MenuItem("Enable Scanlines", "", &config_video.scanlines);
                ImGui::MenuItem("Enable Scanlines Filter", "", &config_video.scanlines_filter);
                ImGui::SliderFloat("##scanlines", &config_video.scanlines_intensity, 0.0f, 1.0f, "Intensity = %.2f");
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("Palette"))
            {
                ImGui::PushItemWidth(130.0f);
                if (ImGui::Combo("##palette", &config_video.palette, "Coleco\0TMS9918\0Custom\0\0", 11))
                {
                    update_palette();
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Custom Palette"))
            {
                for (int i = 0; i < 16; i++)
                {
                    char text[10] = {0};
                    snprintf(text, sizeof(text),"Color #%d", i + 1);
                    if (ImGui::ColorEdit3(text, (float*)&custom_palette[i], ImGuiColorEditFlags_NoInputs))
                    {
                        update_palette();
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Input"))
        {
            gui_in_use = true;

            if (ImGui::BeginMenu("Keyboard"))
            {
                if (ImGui::BeginMenu("Player 1"))
                {
                    keyboard_configuration_item("Left:", &config_input[0].key_left, 0);
                    keyboard_configuration_item("Right:", &config_input[0].key_right, 0);
                    keyboard_configuration_item("Up:", &config_input[0].key_up, 0);
                    keyboard_configuration_item("Down:", &config_input[0].key_down, 0);
                    keyboard_configuration_item("Yellow (Left):", &config_input[0].key_left_button, 0);
                    keyboard_configuration_item("Red (Right):", &config_input[0].key_right_button, 0);
                    keyboard_configuration_item("Purple:", &config_input[0].key_purple, 0);
                    keyboard_configuration_item("Blue:", &config_input[0].key_blue, 0);
                    keyboard_configuration_item("Keypad 0:", &config_input[0].key_0, 0);
                    keyboard_configuration_item("Keypad 1:", &config_input[0].key_1, 0);
                    keyboard_configuration_item("Keypad 2:", &config_input[0].key_2, 0);
                    keyboard_configuration_item("Keypad 3:", &config_input[0].key_3, 0);
                    keyboard_configuration_item("Keypad 4:", &config_input[0].key_4, 0);
                    keyboard_configuration_item("Keypad 5:", &config_input[0].key_5, 0);
                    keyboard_configuration_item("Keypad 6:", &config_input[0].key_6, 0);
                    keyboard_configuration_item("Keypad 7:", &config_input[0].key_7, 0);
                    keyboard_configuration_item("Keypad 8:", &config_input[0].key_8, 0);
                    keyboard_configuration_item("Keypad 9:", &config_input[0].key_9, 0);
                    keyboard_configuration_item("Keypad *:", &config_input[0].key_asterisk, 0);
                    keyboard_configuration_item("Keypad #:", &config_input[0].key_hash, 0);

                    popup_modal_keyboard();

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Player 2"))
                {
                    keyboard_configuration_item("Left:", &config_input[1].key_left, 1);
                    keyboard_configuration_item("Right:", &config_input[1].key_right, 1);
                    keyboard_configuration_item("Up:", &config_input[1].key_up, 1);
                    keyboard_configuration_item("Down:", &config_input[1].key_down, 1);
                    keyboard_configuration_item("Yellow (Left):", &config_input[1].key_left_button, 1);
                    keyboard_configuration_item("Red (Right):", &config_input[1].key_right_button, 1);
                    keyboard_configuration_item("Purple:", &config_input[1].key_purple, 1);
                    keyboard_configuration_item("Blue:", &config_input[1].key_blue, 1);
                    keyboard_configuration_item("Keypad 0:", &config_input[1].key_0, 1);
                    keyboard_configuration_item("Keypad 1:", &config_input[1].key_1, 1);
                    keyboard_configuration_item("Keypad 2:", &config_input[1].key_2, 1);
                    keyboard_configuration_item("Keypad 3:", &config_input[1].key_3, 1);
                    keyboard_configuration_item("Keypad 4:", &config_input[1].key_4, 1);
                    keyboard_configuration_item("Keypad 5:", &config_input[1].key_5, 1);
                    keyboard_configuration_item("Keypad 6:", &config_input[1].key_6, 1);
                    keyboard_configuration_item("Keypad 7:", &config_input[1].key_7, 1);
                    keyboard_configuration_item("Keypad 8:", &config_input[1].key_8, 1);
                    keyboard_configuration_item("Keypad 9:", &config_input[1].key_9, 1);
                    keyboard_configuration_item("Keypad *:", &config_input[1].key_asterisk, 1);
                    keyboard_configuration_item("Keypad #", &config_input[1].key_hash, 1);

                    popup_modal_keyboard();

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Gamepads"))
            {
                if (ImGui::BeginMenu("Player 1"))
                {
                    ImGui::MenuItem("Enable Gamepad P1", "", &config_input[0].gamepad);

                    if (ImGui::BeginMenu("Directional Controls"))
                    {
                        ImGui::PushItemWidth(150.0f);
                        ImGui::Combo("##directional", &config_input[0].gamepad_directional, "D-pad\0Left Analog Stick\0\0");
                        ImGui::PopItemWidth();
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Button Configuration"))
                    {
                        gamepad_configuration_item("Yellow (Left):", &config_input[0].gamepad_left_button, 0);
                        gamepad_configuration_item("Red (Right):", &config_input[0].gamepad_right_button, 0);
                        gamepad_configuration_item("Purple:", &config_input[0].gamepad_purple, 0);
                        gamepad_configuration_item("Blue:", &config_input[0].gamepad_blue, 0);
                        gamepad_configuration_item("Keypad 0:", &config_input[0].gamepad_0, 0);
                        gamepad_configuration_item("Keypad 1:", &config_input[0].gamepad_1, 0);
                        gamepad_configuration_item("Keypad 2:", &config_input[0].gamepad_2, 0);
                        gamepad_configuration_item("Keypad 3:", &config_input[0].gamepad_3, 0);
                        gamepad_configuration_item("Keypad 4:", &config_input[0].gamepad_4, 0);
                        gamepad_configuration_item("Keypad 5:", &config_input[0].gamepad_5, 0);
                        gamepad_configuration_item("Keypad 6:", &config_input[0].gamepad_6, 0);
                        gamepad_configuration_item("Keypad 7:", &config_input[0].gamepad_7, 0);
                        gamepad_configuration_item("Keypad 8:", &config_input[0].gamepad_8, 0);
                        gamepad_configuration_item("Keypad 9:", &config_input[0].gamepad_9, 0);
                        gamepad_configuration_item("Asterisk:", &config_input[0].gamepad_asterisk, 0);
                        gamepad_configuration_item("Hash:", &config_input[0].gamepad_hash, 0);

                        popup_modal_gamepad(0);                 

                        ImGui::EndMenu();
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Player 2"))
                {
                    ImGui::MenuItem("Enable Gamepad P2", "", &config_input[1].gamepad);

                    if (ImGui::BeginMenu("Directional Controls"))
                    {
                        ImGui::PushItemWidth(150.0f);
                        ImGui::Combo("##directional", &config_input[1].gamepad_directional, "D-pad\0Left Analog Stick\0\0");
                        ImGui::PopItemWidth();
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Button Configuration"))
                    {
                        gamepad_configuration_item("Yellow (Left):", &config_input[1].gamepad_left_button, 1);
                        gamepad_configuration_item("Red (Right):", &config_input[1].gamepad_right_button, 1);
                        gamepad_configuration_item("Purple:", &config_input[1].gamepad_purple, 1);
                        gamepad_configuration_item("Blue:", &config_input[1].gamepad_blue, 1);
                        gamepad_configuration_item("Keypad 0:", &config_input[1].gamepad_0, 1);
                        gamepad_configuration_item("Keypad 1:", &config_input[1].gamepad_1, 1);
                        gamepad_configuration_item("Keypad 2:", &config_input[1].gamepad_2, 1);
                        gamepad_configuration_item("Keypad 3:", &config_input[1].gamepad_3, 1);
                        gamepad_configuration_item("Keypad 4:", &config_input[1].gamepad_4, 1);
                        gamepad_configuration_item("Keypad 5:", &config_input[1].gamepad_5, 1);
                        gamepad_configuration_item("Keypad 6:", &config_input[1].gamepad_6, 1);
                        gamepad_configuration_item("Keypad 7:", &config_input[1].gamepad_7, 1);
                        gamepad_configuration_item("Keypad 8:", &config_input[1].gamepad_8, 1);
                        gamepad_configuration_item("Keypad 9:", &config_input[1].gamepad_9, 1);
                        gamepad_configuration_item("Asterisk:", &config_input[1].gamepad_asterisk, 1);
                        gamepad_configuration_item("Hash:", &config_input[1].gamepad_hash, 1);

                        popup_modal_gamepad(1);                 

                        ImGui::EndMenu();
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Spinners"))
            {
                ImGui::MenuItem("Capture Mouse", "F12", &config_emulator.capture_mouse);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("When enabled, the mouse will be captured inside\nthe emulator window to use spinners freely.\nPress F12 to release the mouse.");
                }

                ImGui::Combo("##spinner", &config_emulator.spinner, "Disabled\0Super Action Controller\0Steering Wheel\0Roller Controller\0\0", 4);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("· SAC Spinner for P1 is controlled with mouse movement.\n· SAC Spinner for P2 is controlled with mouse wheel.\n· Steering Wheel is controlled with mouse movement.\n· Roller Controller is controlled with mouse movement and mouse buttons.");
                }
                ImGui::SliderInt("##spinner_sensitivity", &config_emulator.spinner_sensitivity, 1, 10, "Sensitivity = %d");

                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Audio"))
        {
            gui_in_use = true;

            if (ImGui::MenuItem("Enable Audio", "", &config_audio.enable))
            {
                emu_audio_mute(!config_audio.enable);
            }

            if (ImGui::MenuItem("Sync With Emulator", "", &config_audio.sync))
            {
                config_emulator.ffwd = false;

                if (!config_audio.sync)
                {
                    config_video.sync = false;
                    SDL_GL_SetSwapInterval(0);
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug"))
        {
            gui_in_use = true;

            if (ImGui::MenuItem("Enable", "", &config_debug.debug))
            {
                emu_set_overscan(config_debug.debug ? 0 : config_video.overscan);

                if (config_debug.debug)
                    emu_debug_step();
                else
                    emu_debug_continue();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Step Over", "CTRL + F10", (void*)0, config_debug.debug))
            {
                emu_debug_step();
            }

            if (ImGui::MenuItem("Step Frame", "CTRL + F6", (void*)0, config_debug.debug))
            {
                emu_debug_next_frame();
                gui_debug_memory_step_frame();
            }

            if (ImGui::MenuItem("Continue", "CTRL + F5", (void*)0, config_debug.debug))
            {
                emu_debug_continue();
            }

            if (ImGui::MenuItem("Run To Cursor", "CTRL + F8", (void*)0, config_debug.debug))
            {
                gui_debug_runtocursor();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Go Back", "CTRL + BACKSPACE", (void*)0, config_debug.debug))
            {
                gui_debug_go_back();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Toggle Breakpoint", "CTRL + F9", (void*)0, config_debug.debug))
            {
                gui_debug_toggle_breakpoint();
            }

            if (ImGui::MenuItem("Clear All Processor Breakpoints", 0, (void*)0, config_debug.debug))
            {
                gui_debug_reset_breakpoints_cpu();
            }

             if (ImGui::MenuItem("Clear All Memory Breakpoints", 0, (void*)0, config_debug.debug))
            {
                gui_debug_reset_breakpoints_mem();
            }

            ImGui::MenuItem("Disable All Processor Breakpoints", 0, &emu_debug_disable_breakpoints_cpu, config_debug.debug);

            ImGui::MenuItem("Disable All Memory Breakpoints", 0, &emu_debug_disable_breakpoints_mem, config_debug.debug);

            ImGui::Separator();

            if (ImGui::BeginMenu("Font Size", config_debug.debug))
            {
                ImGui::PushItemWidth(110.0f);
                if (ImGui::Combo("##font", &config_debug.font_size, "Very Small\0Small\0Medium\0Large\0\0"))
                {
                    gui_default_font = default_font[config_debug.font_size];
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            ImGui::Separator();

            ImGui::MenuItem("Show Output Screen", "", &config_debug.show_screen, config_debug.debug);

            ImGui::MenuItem("Show Disassembler", "", &config_debug.show_disassembler, config_debug.debug);

            ImGui::MenuItem("Show Z80 Status", "", &config_debug.show_processor, config_debug.debug);

            ImGui::MenuItem("Show Memory Editor", "", &config_debug.show_memory, config_debug.debug);

            ImGui::MenuItem("Show VRAM Viewer", "", &config_debug.show_video, config_debug.debug);

            ImGui::MenuItem("Show VRAM Registers", "", &config_debug.show_video_registers, config_debug.debug);

            ImGui::Separator();

#if defined(__APPLE__) || defined(_WIN32)
            ImGui::MenuItem("Multi-Viewport (Restart required)", "", &config_debug.multi_viewport, config_debug.debug);
            ImGui::Separator();
#endif

            if (ImGui::MenuItem("Load Symbols...", "", (void*)0, config_debug.debug))
            {
                open_symbols = true;
            }

            if (ImGui::MenuItem("Clear Symbols", "", (void*)0, config_debug.debug))
            {
                gui_debug_reset_symbols();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("About"))
        {
            gui_in_use = true;

            if (ImGui::MenuItem("About " GEARCOLECO_TITLE " " GEARCOLECO_VERSION " ..."))
            {
               open_about = true;
            }
            ImGui::EndMenu();
        }

        main_menu_height = (int)ImGui::GetWindowSize().y;

        ImGui::EndMainMenuBar();       
    }

    if (open_rom || shortcut_open_rom)
    {
        shortcut_open_rom = false;
        file_dialog_open_rom();
    }

    if (open_ram)
        file_dialog_load_ram();

    if (save_ram)
        file_dialog_save_ram();

    if (open_state)
        file_dialog_load_state();
    
    if (save_state)
        file_dialog_save_state();

    if (save_screenshot)
        file_dialog_save_screenshot();

    if (choose_savestates_path)
        file_dialog_choose_savestate_path();

    if (open_bios)
        file_dialog_load_bios();

    if (open_symbols)
        file_dialog_load_symbols();

    if (open_about)
    {
        dialog_in_use = true;
        ImGui::OpenPopup("About " GEARCOLECO_TITLE);
    }

    if (open_bios_warning)
    {
        dialog_in_use = true;
        ImGui::OpenPopup("BIOS");
    }

    popup_modal_bios();
    popup_modal_about();

    for (int i = 0; i < 16; i++)
        config_video.color[i] = color_float_to_int(custom_palette[i]);
}

static void main_window(void)
{
    GC_RuntimeInfo runtime;
    emu_get_runtime(runtime);

    int w = (int)ImGui::GetIO().DisplaySize.x;
    int h = (int)ImGui::GetIO().DisplaySize.y - (config_emulator.show_menu ? main_menu_height : 0);

    int selected_ratio = config_debug.debug ? 0 : config_video.ratio;
    float ratio = 0;

    switch (selected_ratio)
    {
        case 1:
            ratio = 4.0f / 3.0f;
            break;
        case 2:
            ratio = 16.0f / 9.0f;
            break;
        case 3:
            ratio = 16.0f / 10.0f;
            break;
        default:
            ratio = (float)runtime.screen_width / (float)runtime.screen_height;
    }

    if (!config_debug.debug && config_video.scale == 3)
    {
        ratio = (float)w / (float)h;
    }

    int w_corrected = (int)(runtime.screen_height * ratio);
    int h_corrected = (int)(runtime.screen_height);
    int scale_multiplier = 0;

    if (config_debug.debug)
    {
        if (config_video.scale != 0)
            scale_multiplier = config_video.scale_manual;
        else
            scale_multiplier = 1;
    }
    else
    {
        switch (config_video.scale)
        {
        case 0:
        {
            int factor_w = w / w_corrected;
            int factor_h = h / h_corrected;
            scale_multiplier = (factor_w < factor_h) ? factor_w : factor_h;
            break;
        }
        case 1:
            scale_multiplier = config_video.scale_manual;
            break;
        case 2:
            scale_multiplier = 1;
            h_corrected = h;
            w_corrected = (int)(h * ratio);
            break;
        case 3:
            scale_multiplier = 1;
            w_corrected = w;
            h_corrected = h;
            break;
        default:
            scale_multiplier = 1;
            break;
        }
    }

    main_window_width = w_corrected * scale_multiplier;
    main_window_height = h_corrected * scale_multiplier;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar;
    
    if (config_debug.debug)
    {
        flags |= ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::SetNextWindowPos(ImVec2(568, 31), ImGuiCond_FirstUseEver);

        ImGui::Begin("Output###debug_output", &config_debug.show_screen, flags);
        gui_main_window_hovered = ImGui::IsWindowHovered();
    }
    else
    {
        int window_x = (w - (w_corrected * scale_multiplier)) / 2;
        int window_y = ((h - (h_corrected * scale_multiplier)) / 2) + (config_emulator.show_menu ? main_menu_height : 0);

        ImGui::SetNextWindowSize(ImVec2((float)main_window_width, (float)main_window_height));
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos + ImVec2((float)window_x, (float)window_y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin(GEARCOLECO_TITLE, 0, flags);
        gui_main_window_hovered = ImGui::IsWindowHovered();
    }

    float tex_h = (float)runtime.screen_width / (float)(GC_RESOLUTION_WIDTH_WITH_OVERSCAN);
    float tex_v = (float)runtime.screen_height / (float)(GC_RESOLUTION_HEIGHT_WITH_OVERSCAN);

    ImGui::Image((ImTextureID)(intptr_t)renderer_emu_texture, ImVec2((float)main_window_width, (float)main_window_height), ImVec2(0, 0), ImVec2(tex_h, tex_v));

    if (config_video.fps)
        show_fps();

    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    if (!config_debug.debug)
    {
        ImGui::PopStyleVar();
    }
}

static void file_dialog_open_rom(void)
{
    nfdchar_t *outPath;
    nfdfilteritem_t filterItem[1] = { { "ROM Files", "col,cv,rom,bin,zip" } };
    nfdopendialogu8args_t args = { };
    args.filterList = filterItem;
    args.filterCount = 1;
    args.defaultPath = config_emulator.last_open_path.c_str();
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        std::string path = outPath;
        std::string::size_type pos = path.find_last_of("\\/");
        config_emulator.last_open_path.assign(path.substr(0, pos));
        gui_load_rom(outPath);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Open ROM Error: %s", NFD_GetError());
    }
}

static void file_dialog_load_ram(void)
{
    nfdchar_t *outPath;
    nfdfilteritem_t filterItem[1] = { { "RAM Files", "sav" } };
    nfdopendialogu8args_t args = { };
    args.filterList = filterItem;
    args.filterCount = 1;
    args.defaultPath = config_emulator.last_open_path.c_str();
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        Cartridge::ForceConfiguration config;
        config.region = get_region(config_emulator.region);
        config.type = Cartridge::CartridgeNotSupported;

        emu_load_ram(outPath, config);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Load RAM Error: %s", NFD_GetError());
    }
}

static void file_dialog_save_ram(void)
{
    nfdchar_t *outPath;
    nfdfilteritem_t filterItem[1] = { { "RAM Files", "sav" } };
    nfdsavedialogu8args_t args = { };
    args.filterList = filterItem;
    args.filterCount = 1;
    args.defaultPath = config_emulator.last_open_path.c_str();
    args.defaultName = NULL;
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        emu_save_ram(outPath);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Save RAM Error: %s", NFD_GetError());
    }
}

static void file_dialog_load_state(void)
{
    nfdchar_t *outPath;
    nfdfilteritem_t filterItem[1] = { { "Save State Files", "state" } };
    nfdopendialogu8args_t args = { };
    args.filterList = filterItem;
    args.filterCount = 1;
    args.defaultPath = config_emulator.last_open_path.c_str();
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        std::string message("Loading state from ");
        message += outPath;
        gui_set_status_message(message.c_str(), 3000);
        emu_load_state_file(outPath);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Load State Error: %s", NFD_GetError());
    }
}

static void file_dialog_save_state(void)
{
    nfdchar_t *outPath;
    nfdfilteritem_t filterItem[1] = { { "Save State Files", "state" } };
    nfdsavedialogu8args_t args = { };
    args.filterList = filterItem;
    args.filterCount = 1;
    args.defaultPath = config_emulator.last_open_path.c_str();
    args.defaultName = NULL;
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        std::string message("Saving state to ");
        message += outPath;
        gui_set_status_message(message.c_str(), 3000);
        emu_save_state_file(outPath);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Save State Error: %s", NFD_GetError());
    }
}

static void file_dialog_choose_savestate_path(void)
{
    nfdchar_t *outPath;
    nfdpickfolderu8args_t args = { };
    args.defaultPath = savestates_path;
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_PickFolderU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        strcpy(savestates_path, outPath);
        config_emulator.savestates_path.assign(outPath);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Savestate Path Error: %s", NFD_GetError());
    }
}

static void file_dialog_load_bios(void)
{
    nfdchar_t *outPath;
    nfdfilteritem_t filterItem[1] = { { "BIOS Files", "bin,rom,bios,cv,col" } };
    nfdopendialogu8args_t args = { };
    args.filterList = filterItem;
    args.filterCount = 1;
    args.defaultPath = config_emulator.last_open_path.c_str();
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        strcpy(bios_path, outPath);
        config_emulator.bios_path.assign(outPath);
        emu_load_bios(bios_path);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Load Bios Error: %s", NFD_GetError());
    }
}

static void file_dialog_load_symbols(void)
{
    nfdchar_t *outPath;
    nfdfilteritem_t filterItem[1] = { { "Symbol Files", "sym" } };
    nfdopendialogu8args_t args = { };
    args.filterList = filterItem;
    args.filterCount = 1;
    args.defaultPath = NULL;
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        gui_debug_reset_symbols();
        gui_debug_load_symbols_file(outPath);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Load Symbols Error: %s", NFD_GetError());
    }
}

static void file_dialog_save_screenshot(void)
{
    nfdchar_t *outPath;
    nfdfilteritem_t filterItem[1] = { { "PNG Files", "png" } };
    nfdsavedialogu8args_t args = { };
    args.filterList = filterItem;
    args.filterCount = 1;
    args.defaultPath = NULL;
    args.defaultName = NULL;
    file_dialog_set_native_window(application_sdl_window, &args.parentWindow);

    nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
    {
        call_save_screenshot(outPath);
        NFD_FreePath(outPath);
    }
    else if (result != NFD_CANCEL)
    {
        Log("Save Screenshot Error: %s", NFD_GetError());
    }
}

static void file_dialog_set_native_window(SDL_Window* window, nfdwindowhandle_t* native_window)
{
    if (!NFD_GetNativeWindowFromSDLWindow(window, native_window))
    {
        Log("NFD_GetNativeWindowFromSDLWindow failed: %s\n", SDL_GetError());
    }
}

static void keyboard_configuration_item(const char* text, SDL_Scancode* key, int player)
{
    ImGui::Text("%s", text);
    ImGui::SameLine(100);

    char button_label[256];
    snprintf(button_label, sizeof(button_label), "%s##%s%d", SDL_GetKeyName(SDL_GetKeyFromScancode(*key)), text, player);

    if (ImGui::Button(button_label, ImVec2(90,0)))
    {
        configured_key = key;
        ImGui::OpenPopup("Keyboard Configuration");
    }

    ImGui::SameLine();

    char remove_label[256];
    snprintf(remove_label, sizeof(remove_label), "X##rk%s%d", text, player);

    if (ImGui::Button(remove_label))
    {
        *key = SDL_SCANCODE_UNKNOWN;
    }
}

static void gamepad_configuration_item(const char* text, int* button, int player)
{
    ImGui::Text("%s", text);
    ImGui::SameLine(100);

    static const char* gamepad_names[16] = {"A", "B", "X" ,"Y", "BACK", "GUIDE", "START", "L3", "R3", "L1", "R1", "UP", "DOWN", "LEFT", "RIGHT", "15"};

    const char* button_name = (*button >= 0 && *button < 16) ? gamepad_names[*button] : "";

    char button_label[256];
    snprintf(button_label, sizeof(button_label), "%s##%s%d", button_name, text, player);

    if (ImGui::Button(button_label, ImVec2(70,0)))
    {
        configured_button = button;
        ImGui::OpenPopup("Gamepad Configuration");
    }

    ImGui::SameLine();

    char remove_label[256];
    snprintf(remove_label, sizeof(remove_label), "X##rg%s%d", text, player);

    if (ImGui::Button(remove_label))
    {
        *button = SDL_CONTROLLER_BUTTON_INVALID;
    }
}

static void popup_modal_keyboard()
{
    if (ImGui::BeginPopupModal("Keyboard Configuration", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Press any key to assign...\n\n");
        ImGui::Separator();

        for (ImGuiKey i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; i = (ImGuiKey)(i + 1))
        {
            if (ImGui::IsKeyDown(i))
            {
                SDL_Keycode key_code = ImGuiKeyToSDLKeycode(i);
                SDL_Scancode key = SDL_GetScancodeFromKey(key_code);

                if ((key != SDL_SCANCODE_LCTRL) && (key != SDL_SCANCODE_RCTRL) && (key != SDL_SCANCODE_CAPSLOCK))
                {
                    *configured_key = key;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
        }

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void popup_modal_gamepad(int pad)
{
    if (ImGui::BeginPopupModal("Gamepad Configuration", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Press any button in your gamepad...\n\n");
        ImGui::Separator();

        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
        {
            if (SDL_GameControllerGetButton(application_gamepad[pad], (SDL_GameControllerButton)i))
            {
                *configured_button = i;
                ImGui::CloseCurrentPopup();
                break;
            }
        }

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void popup_modal_about(void)
{
    if (ImGui::BeginPopupModal("About " GEARCOLECO_TITLE, NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushFont(gui_default_font);
        ImGui::TextColored(cyan, "%s\n", GEARCOLECO_TITLE_ASCII);

        ImGui::TextColored(orange, "  By Ignacio Sánchez (DrHelius)");
        ImGui::Text(" "); ImGui::SameLine();
        ImGui::TextLinkOpenURL("https://github.com/drhelius/Gearcoleco");
        ImGui::Text(" "); ImGui::SameLine();
        ImGui::TextLinkOpenURL("https://x.com/drhelius");
        ImGui::NewLine();

        ImGui::PopFont();

        if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None))
        {
            if (ImGui::BeginTabItem("Build Info"))
            {
                ImGui::BeginChild("build", ImVec2(0, 100), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

                ImGui::Text("Build: %s", GEARCOLECO_VERSION);

                #if defined(__DATE__) && defined(__TIME__)
                ImGui::Text("Built on: %s - %s", __DATE__, __TIME__);
                #endif
                #if defined(_M_ARM64)
                ImGui::Text("Windows ARM64 build");
                #endif
                #if defined(_M_X64)
                ImGui::Text("Windows 64 bit build");
                #endif
                #if defined(_M_IX86)
                ImGui::Text("Windows 32 bit build");
                #endif
                #if defined(__linux__) && defined(__x86_64__)
                ImGui::Text("Linux 64 bit build");
                #endif
                #if defined(__linux__) && defined(__i386__)
                ImGui::Text("Linux 32 bit build");
                #endif
                #if defined(__linux__) && defined(__arm__)
                ImGui::Text("Linux ARM build");
                #endif
                #if defined(__linux__) && defined(__aarch64__)
                ImGui::Text("Linux ARM64 build");
                #endif
                #if defined(__APPLE__) && defined(__arm64__ )
                ImGui::Text("macOS build (Apple Silicon)");
                #endif
                #if defined(__APPLE__) && defined(__x86_64__)
                ImGui::Text("macOS build (Intel)");
                #endif
                #if defined(__ANDROID__)
                ImGui::Text("Android build");
                #endif
                #if defined(_MSC_FULL_VER)
                ImGui::Text("Microsoft C++ %d", _MSC_FULL_VER);
                #endif
                #if defined(_MSVC_LANG)
                ImGui::Text("MSVC %d", _MSVC_LANG);
                #endif
                #if defined(__CLR_VER)
                ImGui::Text("CLR version: %d", __CLR_VER);
                #endif
                #if defined(__MINGW32__)
                ImGui::Text("MinGW 32 bit (%d.%d)", __MINGW32_MAJOR_VERSION, __MINGW32_MINOR_VERSION);
                #endif
                #if defined(__MINGW64__)
                ImGui::Text("MinGW 64 bit (%d.%d)", __MINGW64_VERSION_MAJOR, __MINGW64_VERSION_MINOR);
                #endif
                #if defined(__GNUC__) && !defined(__llvm__) && !defined(__INTEL_COMPILER)
                ImGui::Text("GCC %d.%d.%d", (int)__GNUC__, (int)__GNUC_MINOR__, (int)__GNUC_PATCHLEVEL__);
                #endif
                #if defined(__clang_version__)
                ImGui::Text("Clang %s", __clang_version__);
                #endif
                ImGui::Text("SDL %d.%d.%d (build)", application_sdl_build_version.major, application_sdl_build_version.minor, application_sdl_build_version.patch);
                ImGui::Text("SDL %d.%d.%d (link) ", application_sdl_link_version.major, application_sdl_link_version.minor, application_sdl_link_version.patch);
                ImGui::Text("OpenGL %s", renderer_opengl_version);
                #if !defined(__APPLE__)
                ImGui::Text("GLEW %s", renderer_glew_version);
                #endif
                ImGui::Text("Dear ImGui %s (%d)", IMGUI_VERSION, IMGUI_VERSION_NUM);

                #if defined(DEBUG)
                ImGui::Text("define: DEBUG");
                #endif
                #if defined(DEBUG_GEARCOLECO)
                ImGui::Text("define: DEBUG_GEARCOLECO");
                #endif
                #if defined(GEARCOLECO_NO_OPTIMIZATIONS)
                ImGui::Text("define: GEARCOLECO_NO_OPTIMIZATIONS");
                #endif
                #if defined(__cplusplus)
                ImGui::Text("define: __cplusplus = %d", (int)__cplusplus);
                #endif
                #if defined(__STDC__)
                ImGui::Text("define: __STDC__ = %d", (int)__STDC__);
                #endif
                #if defined(__STDC_VERSION__)
                ImGui::Text("define: __STDC_VERSION__ = %d", (int)__STDC_VERSION__);
                #endif
                #if defined(IS_LITTLE_ENDIAN)
                ImGui::Text("define: IS_LITTLE_ENDIAN");
                #endif
                #if defined(IS_BIG_ENDIAN)
                ImGui::Text("define: IS_BIG_ENDIAN");
                #endif
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Special thanks to"))
            {
                ImGui::BeginChild("backers", ImVec2(0, 100), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                ImGui::Text("%s", BACKERS_STR);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("LICENSE"))
            {
                ImGui::BeginChild("license", ImVec2(0, 100), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                ImGui::TextUnformatted(GPL_LICENSE_STR);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::NewLine();
        ImGui::Separator();

        for (int i = 0; i < 2; i++)
        {
            if (application_gamepad[i])
                ImGui::Text("> Gamepad detected for Player %d", i+1);
            else
                ImGui::Text("> No gamepad detected for Player %d", i+1);
        }

        if (application_added_gamepad_mappings || application_updated_gamepad_mappings)
        {
            ImGui::Text("%d game controller mappings added from gamecontrollerdb.txt", application_added_gamepad_mappings);
            ImGui::Text("%d game controller mappings updated from gamecontrollerdb.txt", application_updated_gamepad_mappings);
        }
        else
            ImGui::Text("ERROR: Game controller database not found (gamecontrollerdb.txt)!!");

        ImGui::Separator();
        ImGui::NewLine();

        if (ImGui::Button("OK", ImVec2(120, 0))) 
        {
            ImGui::CloseCurrentPopup();
            dialog_in_use = false;
        }
        ImGui::SetItemDefaultFocus();

        ImGui::EndPopup();
    }
}

static void popup_modal_bios(void)
{
    if (ImGui::BeginPopupModal("BIOS", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {      
        ImGui::Text("IMPORTANT! ColecoVision BIOS is required to run ROMs.");
        ImGui::Text(" ");
        ImGui::Text("Load a BIOS file using the \"Emulator -> BIOS -> Load BIOS...\" menu option.");
        ImGui::Text(" ");
        
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) 
        {
            ImGui::CloseCurrentPopup();
            dialog_in_use = false;
        }
        ImGui::SetItemDefaultFocus();

        ImGui::EndPopup();
    }
}

static GC_Color color_float_to_int(ImVec4 color)
{
    GC_Color ret;
    ret.red = (u8)floor(color.x >= 1.0 ? 255.0 : color.x * 256.0);
    ret.green = (u8)floor(color.y >= 1.0 ? 255.0 : color.y * 256.0);
    ret.blue = (u8)floor(color.z >= 1.0 ? 255.0 : color.z * 256.0);
    return ret;
}

static ImVec4 color_int_to_float(GC_Color color)
{
    ImVec4 ret;
    ret.w = 0;
    ret.x = (1.0f / 255.0f) * color.red;
    ret.y = (1.0f / 255.0f) * color.green;
    ret.z = (1.0f / 255.0f) * color.blue;
    return ret;
}

static void update_palette(void)
{
    if (config_video.palette == 2)
    {
        emu_palette(config_video.color);
    }
    else
        emu_predefined_palette(config_video.palette);
}

static void push_recent_rom(std::string path)
{
    int slot = 0;
    for (slot = 0; slot < config_max_recent_roms; slot++)
    {
        if (config_emulator.recent_roms[slot].compare(path) == 0)
        {
            break;
        }
    }

    slot = std::min(slot, config_max_recent_roms - 1);

    for (int i = slot; i > 0; i--)
    {
        config_emulator.recent_roms[i] = config_emulator.recent_roms[i - 1];
    }

    config_emulator.recent_roms[0] = path;
}

static void menu_reset(void)
{
    gui_set_status_message("Resetting...", 3000);

    emu_resume();

    Cartridge::ForceConfiguration config;

    config.region = get_region(config_emulator.region);
    config.type = Cartridge::CartridgeNotSupported;

    emu_reset(config);

    if (config_emulator.start_paused)
    {
        emu_pause();
        
        for (int i=0; i < (GC_RESOLUTION_WIDTH_WITH_OVERSCAN * GC_RESOLUTION_HEIGHT_WITH_OVERSCAN); i++)
        {
            emu_frame_buffer[i] = 0;
        }
    }
}

static void menu_pause(void)
{
    if (emu_is_paused())
    {
        gui_set_status_message("Resumed", 3000);
        emu_resume();
    }
    else
    {
        gui_set_status_message("Paused", 3000);
        emu_pause();
    }
}

static void menu_ffwd(void)
{
    config_audio.sync = !config_emulator.ffwd;

    if (config_emulator.ffwd)
    {
        gui_set_status_message("Fast Forward ON", 3000);
        SDL_GL_SetSwapInterval(0);
    }
    else
    {
        gui_set_status_message("Fast Forward OFF", 3000);
        SDL_GL_SetSwapInterval(config_video.sync ? 1 : 0);
        emu_audio_reset();
    }
}

static void show_info(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::Begin("ROM Info", &config_emulator.show_info, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

    static char info[512];
    emu_get_info(info);

    ImGui::PushFont(gui_default_font);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.502f,0.957f,1.0f));
    ImGui::SetCursorPosX(5.0f);
    ImGui::Text("%s", info);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void show_fps(void)
{
    ImGui::PushFont(gui_default_font);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,1.00f,0.0f,1.0f));
    ImGui::SetCursorPos(ImVec2(5.0f, config_debug.debug ? 25.0f : 5.0f));
    ImGui::Text("FPS:  %.2f\nTIME: %.2f ms", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static void show_status_message(void)
{
    if (status_message_active)
    {
        u32 current_time = SDL_GetTicks();
        if ((current_time - status_message_start_time) > status_message_duration)
            status_message_active = false;
        else
            ImGui::OpenPopup("Status");
    }

    if (status_message_active)
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, config_emulator.show_menu ? main_menu_height : 0.0f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.9f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav;

        if (ImGui::BeginPopup("Status", flags))
        {
            ImGui::PushFont(gui_default_font);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f,0.9f,0.1f,1.0f));
            ImGui::TextWrapped("%s", status_message);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::EndPopup();
        }

        ImGui::PopStyleVar();
    }
}

static Cartridge::CartridgeRegions get_region(int index)
{
    //"Auto\0NTSC (60 Hz)\0PAL (50 Hz)\0\0");
    switch (index)
    {
        case 0:
            return Cartridge::CartridgeUnknownRegion;
        case 1:
            return Cartridge::CartridgeNTSC;
        case 2:
            return Cartridge::CartridgePAL;
        default:
            return Cartridge::CartridgeUnknownRegion;
    }
}

static void call_save_screenshot(const char* path)
{
    using namespace std;

    if (!emu_get_core()->GetCartridge()->IsReady())
        return;

    time_t now = time(0);
    tm* ltm = localtime(&now);

    string date_time = to_string(1900 + ltm->tm_year) + "-" + to_string(1 + ltm->tm_mon) + "-" + to_string(ltm->tm_mday) + " " + to_string(ltm->tm_hour) + to_string(ltm->tm_min) + to_string(ltm->tm_sec);

    string file_path;

    if (path != NULL)
        file_path = path;
    else if ((emu_savestates_dir_option == 0) && (strcmp(emu_savestates_path, "")))
         file_path = file_path.assign(emu_savestates_path)+ "/" + string(emu_get_core()->GetCartridge()->GetFileName()) + " - " + date_time + ".png";
    else
         file_path = file_path.assign(emu_get_core()->GetCartridge()->GetFilePath()) + " - " + date_time + ".png";

    emu_save_screenshot(file_path.c_str());

    string message = "Screenshot saved to " + file_path;
    gui_set_status_message(message.c_str(), 3000);
}

static void set_style(void)
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.6000000238418579f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.WindowRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMinSize = ImVec2(32.0f, 32.0f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Left;
    style.ChildRounding = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 4.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(4.0f, 3.0f);
    style.FrameRounding = 2.5f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.IndentSpacing = 21.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 11.0f;
    style.ScrollbarRounding = 2.5f;
    style.GrabMinSize = 10.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 3.5f;
    style.TabBorderSize = 0.0f;
    style.TabMinWidthForCloseButton = 0.0f;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.5921568870544434f, 0.5921568870544434f, 0.5921568870544434f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.060085229575634f, 0.060085229575634f, 0.06008583307266235f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.05882352963089943f, 0.05882352963089943f, 0.05882352963089943f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.1176470592617989f, 0.1176470592617989f, 0.1176470592617989f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.1802574992179871f, 0.1802556961774826f, 0.1802556961774826f, 1.0f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.3058823645114899f, 0.3058823645114899f, 0.3058823645114899f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1843137294054031f, 0.1843137294054031f, 0.1843137294054031f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.270386278629303f, 0.2703835666179657f, 0.2703848779201508f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1176470592617989f, 0.1176470592617989f, 0.1176470592617989f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1176470592617989f, 0.1176470592617989f, 0.1176470592617989f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.6266094446182251f, 0.6266031861305237f, 0.6266063451766968f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.9999899864196777f, 0.9999899864196777f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.9999899864196777f, 0.9999899864196777f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.184547483921051f, 0.184547483921051f, 0.1845493316650391f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.1843137294054031f, 0.1843137294054031f, 0.1843137294054031f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.1803921610116959f, 0.1803921610116959f, 0.1803921610116959f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.1803921610116959f, 0.1803921610116959f, 0.1803921610116959f, 1.0f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.1803921610116959f, 0.1803921610116959f, 0.1803921610116959f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.2489270567893982f, 0.2489245682954788f, 0.2489245682954788f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 0.9999899864196777f, 0.9999899864196777f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 0.9999899864196777f, 0.9999899864196777f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05999999865889549f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.5764706f, 0.44705882f, 0.36078431f, 1.0f);
    style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.1450980454683304f, 0.1450980454683304f, 0.1490196138620377f, 0.7f);

    style.Colors[ImGuiCol_DockingPreview] = style.Colors[ImGuiCol_HeaderActive] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
    style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_HeaderHovered];
    style.Colors[ImGuiCol_TabSelected] = lerp(style.Colors[ImGuiCol_HeaderActive], style.Colors[ImGuiCol_TitleBgActive], 0.60f);
    style.Colors[ImGuiCol_TabSelectedOverline] = style.Colors[ImGuiCol_HeaderActive];
    style.Colors[ImGuiCol_TabDimmed] = lerp(style.Colors[ImGuiCol_Tab], style.Colors[ImGuiCol_TitleBg], 0.80f);
    style.Colors[ImGuiCol_TabDimmedSelected] = lerp(style.Colors[ImGuiCol_TabSelected], style.Colors[ImGuiCol_TitleBg], 0.40f);
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
}

static ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
