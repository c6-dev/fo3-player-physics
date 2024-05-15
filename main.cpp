#include <cstddef>
#include <Windows.h>

using enum hkpCharacterState::StateType;

struct IniPrefSetting {
	void **vtable;
	float value;
	const char *name;

	IniPrefSetting(const char *name, float value)
	{
		ThisCall(0x4DE370, this, name, value);
	}
};

struct CharacterMoveParams {
	// this struct alignment pisses me off
	float multiplier;
	AlignedVector4 forward;
	AlignedVector4 up;
	AlignedVector4 groundNormal;
	AlignedVector4 velocity;
	AlignedVector4 input;
	float maxSpeed;
	AlignedVector4 surfaceVelocity;
};

namespace ini {
//auto fAcceleration = IniPrefSetting("fAcceleration:Movement", 5.f);
//auto fFriction = IniPrefSetting("fFriction:Movement", 5.f);
//auto fStopSpeed = IniPrefSetting("fStopSpeed:Movement", .3f);
constexpr auto fFriction = 5.f;
constexpr auto fAcceleration = 6.f;
constexpr auto fAirAcceleration = 10.f;
constexpr auto fStopSpeed = 8.f;
constexpr auto fAirSpeed = .1f;
}

static void ApplyFriction(
	const CharacterMoveParams &move,
	AlignedVector4 *velocity,
	float deltaTime)
{
	const auto speed = ((NiVector3&)*velocity).Length();
	const auto friction = ini::fFriction * std::max(speed, ini::fStopSpeed) * deltaTime;

	if (friction >= speed)
		*velocity = AlignedVector4(0, 0, 0, 0);
	else
		*velocity *= 1.f - friction / speed;
}

static void ApplyAcceleration(
	const CharacterMoveParams &move,
	AlignedVector4 *velocity,
	UInt32 state,
	const NiVector3 &moveVector,
	float moveLength,
	float deltaTime)
{
	const auto inAir = state == kState_InAir;
	const auto speed = ((NiVector3&)*velocity).DotProduct(moveVector);
	const auto maxSpeed = inAir ? moveLength * ini::fAirSpeed : moveLength;

	if (speed < maxSpeed) {
		const auto accelMultiplier = inAir ? ini::fAirAcceleration : ini::fAcceleration;
		const auto accel = accelMultiplier * moveLength * deltaTime;
		*velocity += moveVector * std::min(accel, maxSpeed - speed);
	}
}

static void UpdateVelocity(
	const CharacterMoveParams &move,
	AlignedVector4 *velocity,
	UInt32 state,
	float deltaTime)
{
	if (state != kState_InAir)
		ApplyFriction(move, velocity, deltaTime);

	const auto moveLength = ((NiVector3&)move.input).Length();

	if (moveLength < 1e-4f)
		return;

	const auto input = AlignedVector4(move.input * (1.f / moveLength));
	const auto &forward = move.forward;
	const auto &up = move.up;
	const auto right = AlignedVector4(((NiVector3&)forward).CrossProduct(up));
	const auto moveVector = forward * -input.x + right * input.y + up * input.z;
	ApplyAcceleration(move, velocity, state, moveVector, moveLength, deltaTime);
}

static void hook_MoveCharacter(
	bhkCharacterController *charCtrl,
	const CharacterMoveParams &move,
	AlignedVector4 *velocity)
{
	if (charCtrl != PlayerCharacter::GetSingleton()->GetCharacterController()) {
		// call original
		CdeclCall(0xD6AEF0, &move, velocity);
		return;
	}

	const auto state = charCtrl->chrContext.hkState;
	const auto deltaTime = charCtrl->stepInfo.deltaTime;
	*velocity -= move.surfaceVelocity.PS();
	UpdateVelocity(move, velocity, state, deltaTime);
	*velocity += move.surfaceVelocity.PS();
}

static __declspec(naked) void hook_MoveCharacter_wrapper()
{
	__asm {
		push [esp+8]
		push [esp+8]
		push esi
		call hook_MoveCharacter
		add esp, 12
		ret
	}
}

static void patch_call_rel32(const uintptr_t addr, const void *dest)
{
	DWORD old_protect;
	VirtualProtect((void*)addr, 5, PAGE_EXECUTE_READWRITE, &old_protect);
	*(char*)addr = '\xE8'; // CALL opcode
	*(std::byte**)(addr + 1) = (std::byte*)dest - addr - 5;
	VirtualProtect((void*)addr, 5, old_protect, &old_protect);
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "Viewmodel Adjustment";
	info->version = 2;
	return true;
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Load(NVSEInterface *nvse)
{
	patch_call_rel32(0xCD414D, hook_MoveCharacter_wrapper);
	patch_call_rel32(0xCD45D0, hook_MoveCharacter_wrapper);
	patch_call_rel32(0xCD4A2A, hook_MoveCharacter_wrapper);
	return true;
}