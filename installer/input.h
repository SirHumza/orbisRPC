#ifndef PS4UTIL_INPUT_H
#define PS4UTIL_INPUT_H

#include <stdint.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>

// Button identifiers, named to match the symbols used across the app.
//
// These are the PS4's own scePad button bits. Input goes through libkernel's
// scePad rather than SDL_Joystick because SDL_PS4's joystick backend did not
// detect a controller on the target console, whereas scePad works reliably
// there. SDL still owns the window and presentation.
//
// To switch to SDL_Joystick later, only this table and the body of
// Input::Update() need to change -- the rest of the app just calls
// Pressed()/Held() with these names.
enum PadButton
{
	kPadCross    = ORBIS_PAD_BUTTON_CROSS,
	kPadCircle   = ORBIS_PAD_BUTTON_CIRCLE,
	kPadSquare   = ORBIS_PAD_BUTTON_SQUARE,
	kPadTriangle = ORBIS_PAD_BUTTON_TRIANGLE,
	kPadL1       = ORBIS_PAD_BUTTON_L1,
	kPadR1       = ORBIS_PAD_BUTTON_R1,
	kPadOptions  = ORBIS_PAD_BUTTON_OPTIONS,
	kPadL3       = ORBIS_PAD_BUTTON_L3,
	kPadR3       = ORBIS_PAD_BUTTON_R3,
	kPadUp       = ORBIS_PAD_BUTTON_UP,
	kPadDown     = ORBIS_PAD_BUTTON_DOWN,
	kPadLeft     = ORBIS_PAD_BUTTON_LEFT,
	kPadRight    = ORBIS_PAD_BUTTON_RIGHT,
	kPadTouchPad = ORBIS_PAD_BUTTON_TOUCH_PAD,
};

// Controller polling with proper rising-edge detection.
//
// The toolchain's own `input` sample exposes helpers called TrianglePressed()
// etc., but they test whether a button is *currently held* rather than
// whether it was pressed since the last poll -- so holding UP would re-fire
// every frame. Pressed() here compares against the previous frame instead.
class Input
{
public:
	// Pass a user ID, or -1 to use the initial (logged-in) user.
	bool Init(int userId = -1);
	void Shutdown();

	// Must be called once per frame, before any query. If no controller could
	// be opened this keeps retrying, so plugging one in later is picked up.
	void Update();

	// True only on the frame the button went down.
	bool Pressed(uint32_t button) const;
	// True for as long as the button is down.
	bool Held(uint32_t button) const;

	bool Connected() const { return connected_; }

	// Stick axes, centred on 0, clamped to -128..127. Up is positive.
	int LeftStickX() const;
	int LeftStickY() const;
	int RightStickX() const;
	int RightStickY() const;

	// Trigger travel, 0..255.
	int L2() const { return connected_ ? data_.analogButtons.l2 : 0; }
	int R2() const { return connected_ ? data_.analogButtons.r2 : 0; }

	uint32_t Buttons() const { return current_; }

private:
	// Frames between retries of scePadOpen after a failure.
	enum { kReopenInterval = 60 };

	bool InitUserService();
	bool OpenPad();

	int          pad_;
	int          userId_;
	uint32_t     previous_;
	uint32_t     current_;
	bool         connected_;
	bool         userServiceReady_;
	int          framesSinceOpenAttempt_;
	OrbisPadData data_;
};

#endif
