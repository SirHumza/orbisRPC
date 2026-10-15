#include "input.h"

#include <string.h>

namespace {

// OrbisPadData reports stick Y inverted relative to screen coordinates
// (0 = pushed up). These helpers normalise to up-positive.
int ClampAxis(int raw)
{
	if (raw < -128)
		return -128;
	if (raw > 127)
		return 127;
	return raw;
}

} // namespace

bool Input::InitUserService()
{
	OrbisUserServiceInitializeParams params;
	params.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;

	if (sceUserServiceInitialize(&params) < 0)
		return false;

	userServiceReady_ = true;
	return true;
}

bool Input::OpenPad()
{
	// Type 0 is ORBIS_PAD_PORT_TYPE_STANDARD, index 0 is player one.
	const int handle = scePadOpen(userId_, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
	if (handle < 0)
		return false;

	pad_ = handle;
	return true;
}

bool Input::Init(int userId)
{
	pad_ = -1;
	userId_ = 1;
	previous_ = 0;
	current_ = 0;
	connected_ = false;
	userServiceReady_ = false;
	framesSinceOpenAttempt_ = 0;
	memset(&data_, 0, sizeof(data_));

	if (scePadInit() != 0)
		return false;

	if (userId < 0)
	{
		if (!InitUserService())
			return false;

		if (sceUserServiceGetInitialUser(&userId_) < 0)
		{
			// No logged-in user is not fatal: fall back to user 1, which is
			// what the pad library wants on a fresh boot.
			userId_ = 1;
		}
	}
	else
	{
		userId_ = userId;
	}

	return OpenPad();
}

void Input::Shutdown()
{
	if (pad_ >= 0)
	{
		scePadClose(pad_);
		pad_ = -1;
	}

	if (userServiceReady_)
	{
		sceUserServiceTerminate();
		userServiceReady_ = false;
	}
}

void Input::Update()
{
	previous_ = current_;
	current_ = 0;

	if (pad_ < 0)
	{
		connected_ = false;
		memset(&data_, 0, sizeof(data_));

		// A controller can be plugged in or woken long after the app started,
		// so keep trying to open one now and then instead of giving up on it.
		if (++framesSinceOpenAttempt_ >= kReopenInterval)
		{
			framesSinceOpenAttempt_ = 0;
			OpenPad();
		}

		return;
	}

	if (scePadReadState(pad_, &data_) < 0)
	{
		// A read failure means the pad went away mid-frame. Drop the state so
		// a disconnected controller cannot leave buttons stuck "held".
		connected_ = false;
		current_ = 0;
		memset(&data_, 0, sizeof(data_));
		return;
	}

	connected_ = (data_.connected != 0);
	current_ = connected_ ? data_.buttons : 0;
}

bool Input::Pressed(uint32_t button) const
{
	return connected_ && (current_ & button) && !(previous_ & button);
}

bool Input::Held(uint32_t button) const
{
	return connected_ && (current_ & button);
}

int Input::LeftStickX() const
{
	return connected_ ? ClampAxis(static_cast<int>(data_.leftStick.x) - 128) : 0;
}

int Input::LeftStickY() const
{
	return connected_ ? ClampAxis(128 - static_cast<int>(data_.leftStick.y)) : 0;
}

int Input::RightStickX() const
{
	return connected_ ? ClampAxis(static_cast<int>(data_.rightStick.x) - 128) : 0;
}

int Input::RightStickY() const
{
	return connected_ ? ClampAxis(128 - static_cast<int>(data_.rightStick.y)) : 0;
}
