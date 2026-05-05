// Target: ESP32-S3-CAM (brain)
// Static menu tree + button-driven navigation. Selecting a leaf calls into
// app_state, which is the same path the BLE/web command dispatcher takes.

#include "menu_controller.h"

#include <Arduino.h>
#include <string.h>

#include "app_state.h"
#include "protocol/frame.h"
#include "protocol/packets.h"
#include "transport/transport.h"

namespace menu_controller {

struct Menu;
struct MenuItem {
    const char* label;
    void      (*action)();      // null when this descends into `submenu`
    const Menu* submenu;        // null when this is a leaf
};

struct Menu {
    const char*     title;
    const MenuItem* items;
    uint8_t         n_items;
    const Menu*     parent;
};

// ─── Leaf actions ────────────────────────────────────────────────────────────
static void act_face_idle()    { app_state::setExpression(FaceMode::IDLE); }
static void act_face_happy()   { app_state::setExpression(FaceMode::HAPPY); }
static void act_face_search()  { app_state::setExpression(FaceMode::SEARCH); }
static void act_face_curious() { app_state::setExpression(FaceMode::CURIOUS); }
static void act_face_sleep()   { app_state::setExpression(FaceMode::SLEEP); }

static void act_mode_manual()  { app_state::setMode(CtrlMode::MANUAL); }
static void act_mode_auto()    { app_state::setMode(CtrlMode::AUTO); }

static void act_drive_fwd()    { app_state::moveForward(); }
static void act_drive_back()   { app_state::moveBack(); }
static void act_drive_left()   { app_state::moveLeft(); }
static void act_drive_right()  { app_state::moveRight(); }
static void act_drive_stop()   { app_state::moveStop(); }

// ─── Menu tree ───────────────────────────────────────────────────────────────
static const MenuItem kFacesItems[] = {
    { "Idle",    act_face_idle,    nullptr },
    { "Happy",   act_face_happy,   nullptr },
    { "Search",  act_face_search,  nullptr },
    { "Curious", act_face_curious, nullptr },
    { "Sleep",   act_face_sleep,   nullptr },
};
static const MenuItem kModesItems[] = {
    { "Manual",  act_mode_manual,  nullptr },
    { "Auto",    act_mode_auto,    nullptr },
};
static const MenuItem kDriveItems[] = {
    { "Forward", act_drive_fwd,    nullptr },
    { "Back",    act_drive_back,   nullptr },
    { "Left",    act_drive_left,   nullptr },
    { "Right",   act_drive_right,  nullptr },
    { "Stop",    act_drive_stop,   nullptr },
};

static const Menu kRoot;
static const Menu kFaces = { "Faces", kFacesItems, sizeof(kFacesItems)/sizeof(kFacesItems[0]), &kRoot };
static const Menu kModes = { "Modes", kModesItems, sizeof(kModesItems)/sizeof(kModesItems[0]), &kRoot };
static const Menu kDrive = { "Drive", kDriveItems, sizeof(kDriveItems)/sizeof(kDriveItems[0]), &kRoot };

static const MenuItem kRootItems[] = {
    { "Modes", nullptr, &kModes },
    { "Faces", nullptr, &kFaces },
    { "Drive", nullptr, &kDrive },
};
static const Menu kRoot = { "PetBot", kRootItems, sizeof(kRootItems)/sizeof(kRootItems[0]), nullptr };

// ─── Navigation state ────────────────────────────────────────────────────────
static const Menu* s_current  = &kRoot;
static uint8_t     s_selected = 0;
static uint8_t     s_seq      = 0;
static bool        s_c6_ready = false;

// ─── Wire helpers ────────────────────────────────────────────────────────────
static void send_set_menu() {
    if (!s_c6_ready) return;
    uint8_t buf[PB_MAX_PAYLOAD];
    size_t  off = 0;

    buf[off++] = s_selected;

    size_t title_len = strnlen(s_current->title, 200);
    if (off + 1 + title_len + 1 > sizeof(buf)) return;
    buf[off++] = (uint8_t)title_len;
    memcpy(&buf[off], s_current->title, title_len);
    off += title_len;

    buf[off++] = s_current->n_items;
    for (uint8_t i = 0; i < s_current->n_items; ++i) {
        size_t l = strnlen(s_current->items[i].label, 200);
        if (off + 1 + l > sizeof(buf)) return;
        buf[off++] = (uint8_t)l;
        memcpy(&buf[off], s_current->items[i].label, l);
        off += l;
    }

    uint8_t enc[PB_MAX_FRAME];
    size_t  n = pb_encode(enc, sizeof(enc), PB_SET_MENU, s_seq++, buf, (uint16_t)off);
    if (n > 0) transport().write(enc, n);
}

void pushDebugLine(const char* text) {
    if (!s_c6_ready || !text) return;
    uint8_t buf[PB_MAX_PAYLOAD];
    size_t  text_len = strnlen(text, 200);
    if (7 + text_len > sizeof(buf)) text_len = sizeof(buf) - 7;
    // x=8 y=140 color=0xFFE0 (yellow) size=1
    buf[0] = 0; buf[1] = 8;
    buf[2] = 0; buf[3] = 140;
    buf[4] = 0xFF; buf[5] = 0xE0;
    buf[6] = 1;
    memcpy(&buf[7], text, text_len);

    uint8_t enc[PB_MAX_FRAME];
    size_t  n = pb_encode(enc, sizeof(enc), PB_DRAW_TEXT, s_seq++, buf,
                          (uint16_t)(7 + text_len));
    if (n > 0) transport().write(enc, n);
}

// ─── Public API ──────────────────────────────────────────────────────────────
void init() {
    s_current  = &kRoot;
    s_selected = 0;
    s_c6_ready = false;
}

void update() { /* nothing periodic yet */ }

void onHelloFromC6() {
    s_c6_ready = true;
    Serial.println("[menu] C6 said HELLO — pushing root menu");
    s_current  = &kRoot;
    s_selected = 0;
    send_set_menu();
}

void onStateChanged() {
    // Today the menu doesn't show live state; future expansion can render
    // "Mode: auto" / "Face: HAPPY" in the title bar from here.
    send_set_menu();
}

// One-button mapping (see ROBOT_FIRMWARE_PLAN.md §9 — only BOOT is wired
// today). Short press = move selection down; long press = select the
// current item; release = no-op.
//
// Once dedicated UP/DOWN/SELECT/BACK buttons are wired (PB_BTN_UP etc.)
// route them here directly.
void onButtonEvent(uint8_t btn_id, uint8_t edge) {
    if (btn_id == PB_BTN_BOOT) {
        if (edge == PB_BTN_PRESS) {
            s_selected = (uint8_t)((s_selected + 1) % s_current->n_items);
            send_set_menu();
        } else if (edge == PB_BTN_LONGPRESS) {
            const MenuItem& it = s_current->items[s_selected];
            if (it.action) {
                Serial.printf("[menu] activate '%s'\n", it.label);
                it.action();
            } else if (it.submenu) {
                Serial.printf("[menu] descend into '%s'\n", it.label);
                s_current  = it.submenu;
                s_selected = 0;
                send_set_menu();
            }
        }
        return;
    }

    // Dedicated nav buttons (when wired).
    switch (btn_id) {
        case PB_BTN_UP:
            if (edge != PB_BTN_PRESS) return;
            s_selected = (uint8_t)((s_selected + s_current->n_items - 1) % s_current->n_items);
            send_set_menu();
            break;
        case PB_BTN_DOWN:
            if (edge != PB_BTN_PRESS) return;
            s_selected = (uint8_t)((s_selected + 1) % s_current->n_items);
            send_set_menu();
            break;
        case PB_BTN_SELECT: {
            if (edge != PB_BTN_PRESS) return;
            const MenuItem& it = s_current->items[s_selected];
            if (it.action) it.action();
            else if (it.submenu) { s_current = it.submenu; s_selected = 0; send_set_menu(); }
            break;
        }
        case PB_BTN_BACK:
            if (edge != PB_BTN_PRESS) return;
            if (s_current->parent) {
                s_current = s_current->parent;
                s_selected = 0;
                send_set_menu();
            }
            break;
    }
}

}  // namespace menu_controller
