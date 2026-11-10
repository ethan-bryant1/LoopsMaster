// Window test. Loads the built plugin the way a host does, plays a WAV through it, sends real
// mouse messages to the window, checks the parameters respond, and saves screenshots as BMP.
// Usage: ui_test <plugin.vst3> <demo.wav> <output folder> <results file>
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// Must match ParamId in source/editor.h.
enum { kBypass = 0, kPreset = 1, kCrossover = 2, kSlope = 3, kBodyLevel = 4, kSubLevel = 7, kOutputGain = 15, kClipOn = 27, kClipDrive = 28, kLimitOn = 31 };
const double kPresetSteps = 13.0; // 14 presets

FILE* results = nullptr;
int failures = 0;
HWND window = nullptr;
HWND plugWindow = nullptr;
IEditController* controller = nullptr;
std::string outDir;

class Handler : public IComponentHandler
{
public:
	std::atomic<int> begins {0}, edits {0}, ends {0};
	tresult PLUGIN_API beginEdit (ParamID) override { ++begins; return kResultOk; }
	tresult PLUGIN_API performEdit (ParamID, ParamValue) override { ++edits; return kResultOk; }
	tresult PLUGIN_API endEdit (ParamID) override { ++ends; return kResultOk; }
	tresult PLUGIN_API restartComponent (int32) override { return kResultOk; }
	tresult PLUGIN_API queryInterface (const TUID, void** obj) override { *obj = nullptr; return kNoInterface; }
	uint32 PLUGIN_API addRef () override { return 1; }
	uint32 PLUGIN_API release () override { return 1; }
} handler;

void check (bool ok, const char* what, double value)
{
	std::fprintf (results, "%s  %-50s %.4f\n", ok ? "PASS" : "FAIL", what, value);
	std::fflush (results);
	if (!ok)
		++failures;
}

double param (ParamID id) { return controller->getParamNormalized (id); }

bool loadWav (const char* path, std::vector<float>& l, std::vector<float>& r)
{
	if (std::strcmp (path, "steady") == 0)
	{
		// A steady 110 Hz tone, to show the three waves at 1x, 2x and 4x the period.
		l.resize (44100 * 2);
		for (size_t i = 0; i < l.size (); ++i)
			l[i] = static_cast<float> (0.6 * std::sin (2.0 * 3.14159265358979 * 110.0 * i / 44100.0));
		r = l;
		return true;
	}
	FILE* f = std::fopen (path, "rb");
	if (!f)
		return false;
	std::fseek (f, 44, SEEK_SET); // written by tools/make_sample.mjs with a plain 44-byte header
	std::vector<int16_t> data;
	int16_t buf[4096];
	size_t got;
	while ((got = std::fread (buf, 2, 4096, f)) > 0)
		data.insert (data.end (), buf, buf + got);
	std::fclose (f);
	l.resize (data.size () / 2);
	r.resize (data.size () / 2);
	for (size_t i = 0; i < l.size (); ++i)
	{
		l[i] = data[i * 2] / 32768.f;
		r[i] = data[i * 2 + 1] / 32768.f;
	}
	return !l.empty ();
}

void savePicture (const char* name)
{
	RECT rc;
	GetClientRect (window, &rc);
	const int w = rc.right, h = rc.bottom;
	HDC screen = GetDC (window);
	HDC mem = CreateCompatibleDC (screen);
	BITMAPINFO info {};
	info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
	info.bmiHeader.biWidth = w;
	info.bmiHeader.biHeight = h;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	void* bits = nullptr;
	HBITMAP bmp = CreateDIBSection (mem, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	HGDIOBJ old = SelectObject (mem, bmp);
	PrintWindow (window, mem, PW_CLIENTONLY | 2 /* PW_RENDERFULLCONTENT */);

	const std::string path = outDir + "\\" + name + ".bmp";
	if (FILE* f = std::fopen (path.c_str (), "wb"))
	{
		BITMAPFILEHEADER head {};
		head.bfType = 0x4D42;
		head.bfOffBits = sizeof head + sizeof info.bmiHeader;
		head.bfSize = head.bfOffBits + w * h * 4;
		std::fwrite (&head, sizeof head, 1, f);
		std::fwrite (&info.bmiHeader, sizeof info.bmiHeader, 1, f);
		std::fwrite (bits, 4, static_cast<size_t> (w) * h, f);
		std::fclose (f);
	}
	SelectObject (mem, old);
	DeleteObject (bmp);
	DeleteDC (mem);
	ReleaseDC (window, screen);
}

void mouse (UINT message, int x, int y, WPARAM keys)
{
	SendMessageW (plugWindow, message, keys, MAKELPARAM (x, y));
}

void dragKnob (int x, int y, int pixelsUp)
{
	mouse (WM_MOUSEMOVE, x, y, 0);
	mouse (WM_LBUTTONDOWN, x, y, MK_LBUTTON);
	for (int i = 1; i <= 4; ++i)
		mouse (WM_MOUSEMOVE, x, y - pixelsUp * i / 4, MK_LBUTTON);
	mouse (WM_LBUTTONUP, x, y - pixelsUp, 0);
}

void click (int x, int y)
{
	mouse (WM_MOUSEMOVE, x, y, 0);
	mouse (WM_LBUTTONDOWN, x, y, MK_LBUTTON);
	mouse (WM_LBUTTONUP, x, y, 0);
}

int step = -1;

void runStep ()
{
	switch (step++)
	{
		case -1:
			savePicture ("0_opening_page");
			break;
		case 0:
			savePicture ("1_default");
			check (plugWindow != nullptr, "Plugin created its own child window", plugWindow != nullptr);
			break;
		case 1:
		{
			const double before = param (kSubLevel);
			const int edits = handler.edits;
			dragKnob (132 + 33, 100 + 108 + 8 + 27, 60); // Sub level knob
			check (param (kSubLevel) > before + 0.1, "Dragging Sub Level up raises it", param (kSubLevel) - before);
			check (handler.edits > edits, "Drag sends edits to the host", handler.edits - edits);
			check (handler.begins.load () == handler.ends.load (), "Every begin edit has a matching end edit", handler.ends.load ());
			break;
		}
		case 2:
		{
			const double before = param (kSlope);
			click (98 + 170 - 20, 100 + 324 + 50); // the "48" segment
			check (std::abs (param (kSlope) - 1.0) < 1e-6 && before != 1.0, "Clicking 48 selects that slope", param (kSlope));
			break;
		}
		case 3:
		{
			click (717, 50);
			check (param (kBypass) >= 0.5, "Clicking Bypass turns it on", param (kBypass));
			break;
		}
		case 4:
			// One step later, so the window has redrawn with the mask's eyes out.
			savePicture ("2_bypassed");
			click (717, 50);
			check (param (kBypass) < 0.5, "Clicking Bypass again turns it off", param (kBypass));
			// Double-click resets a knob to its default.
			controller->setParamNormalized (kOutputGain, 0.9);
			break;
		case 5:
			mouse (WM_MOUSEMOVE, 780 - 18 - 33, 100 + 324 + 25, 0);
			mouse (WM_LBUTTONDOWN, 780 - 18 - 33, 100 + 324 + 25, MK_LBUTTON);
			mouse (WM_LBUTTONUP, 780 - 18 - 33, 100 + 324 + 25, 0);
			mouse (WM_LBUTTONDBLCLK, 780 - 18 - 33, 100 + 324 + 25, MK_LBUTTON);
			mouse (WM_LBUTTONUP, 780 - 18 - 33, 100 + 324 + 25, 0);
			check (std::abs (param (kOutputGain) - 0.5833) < 0.01, "Double-click resets Output to its default", param (kOutputGain));
			break;
		case 6:
			controller->setParamNormalized (kPreset, 3.0 / kPresetSteps); // Two Floors
			break;
		case 8:
			savePicture ("3_two_floors");
			check (std::abs (param (kSubLevel) - 0.5) < 0.01, "Choosing a preset moves the Sub Level value", param (kSubLevel));
			break;
		case 9:
			// The steps above fall on a half-second grid, which lines up with rests in the demo.
			SetTimer (window, 2, 230, nullptr);
			break;
		case 10:
			click (377, 50); // the Master tab
			break;
		case 11:
		{
			savePicture ("5_master_page");
			const double before = param (kClipDrive);
			dragKnob (132 + 66 + 33, 100 + 2 * 104 + 4 + 27, 60); // Clip Drive
			check (param (kClipDrive) > before + 0.1, "Dragging Clip Drive on the Master page raises it", param (kClipDrive) - before);
			break;
		}
		case 12:
		{
			const double before = param (kLimitOn);
			click (46, 100 + 3 * 104 + 78);
			check (std::abs (param (kLimitOn) - before) > 0.5, "Clicking the Limit switch changes it", param (kLimitOn));
			click (46, 100 + 3 * 104 + 78);
			check (std::abs (param (kLimitOn) - before) < 0.01, "Clicking it again restores it", param (kLimitOn));
			break;
		}
		case 13:
			controller->setParamNormalized (kPreset, 9.0 / kPresetSteps); // Master Slam
			break;
		case 15:
			savePicture ("6_master_slam");
			check (std::abs (param (kClipDrive) - 0.5) < 0.01, "Master Slam sets Clip Drive to 12 dB", param (kClipDrive));
			check (param (kSubLevel) < 0.01, "Master Slam turns the layers off", param (kSubLevel));
			controller->setParamNormalized (kPreset, 10.0 / kPresetSteps); // Bass Group
			break;
		case 17:
			savePicture ("7_bass_group");
			break;
		case 18:
			click (247, 50); // the EQ tab
			break;
		case 19:
		{
			savePicture ("8_eq_page");
			const int kGain3 = 51 + 2 * 4 + 2;
			const double before = param (kGain3);
			// The EQ graph spans x 0-780 and y 100-356; band 3 starts at 800 Hz, 0 dB.
			dragKnob (static_cast<int> (780.0 * std::log (40.0) / std::log (1000.0)), 228, 50);
			check (param (kGain3) > before + 0.05, "Dragging EQ point 3 up raises its gain", param (kGain3) - before);
			break;
		}
		case 20:
			savePicture ("9_eq_dragged");
			click (312, 50); // the Multi tab
			break;
		case 22:
			savePicture ("10_multi_page");
			break;
		case 23:
			PostQuitMessage (0);
			break;
		default:
			break;
	}
}

LRESULT CALLBACK windowProc (HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
	if (message == WM_TIMER)
	{
		if (w == 2)
		{
			KillTimer (hwnd, 2);
			savePicture ("4_off_grid");
		}
		else
			runStep ();
		return 0;
	}
	if (message == WM_DESTROY)
	{
		PostQuitMessage (0);
		return 0;
	}
	return DefWindowProcW (hwnd, message, w, l);
}

} // namespace

int WINAPI WinMain (HINSTANCE instance, HINSTANCE, LPSTR, int)
{
	// As hosts do. The plugin's logo depends on it.
	const bool withoutCom = std::strstr (GetCommandLineA (), "--no-com") != nullptr;
	if (!withoutCom)
		CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW (GetCommandLineW (), &argc);
	if (argc != 5)
		return 2;
	const std::string pluginPath = VST3::StringConvert::convert (reinterpret_cast<const char16_t*> (argv[1]));
	const std::string wavPath = VST3::StringConvert::convert (reinterpret_cast<const char16_t*> (argv[2]));
	outDir = VST3::StringConvert::convert (reinterpret_cast<const char16_t*> (argv[3]));
	results = _wfopen (argv[4], L"w");
	if (!results)
		return 2;

	std::vector<float> wavL, wavR;
	check (loadWav (wavPath.c_str (), wavL, wavR), "Demo sample loaded", static_cast<double> (wavL.size ()));

	std::string error;
	auto module = VST3::Hosting::Module::create (pluginPath, error);
	check (module != nullptr, "Plugin module loaded", module != nullptr);
	if (!module)
	{
		std::fprintf (results, "%s\n", error.c_str ());
		return 1;
	}

	HostApplication host;
	auto factory = module->getFactory ();
	factory.setHostContext (&host);

	IPtr<IComponent> component;
	for (auto& info : factory.classInfos ())
		if (info.category () == kVstAudioEffectClass)
			component = factory.createInstance<IComponent> (info.ID ());
	check (component != nullptr, "Plugin instance created", component != nullptr);
	if (!component || component->initialize (&host) != kResultOk)
		return 1;

	FUnknownPtr<IAudioProcessor> processor (component);
	FUnknownPtr<IEditController> edit (component);
	controller = edit;
	controller->setComponentHandler (&handler);

	ProcessSetup setup {kRealtime, kSample32, 512, 44100.0};
	processor->setupProcessing (setup);
	component->activateBus (kAudio, kInput, 0, true);
	component->activateBus (kAudio, kInput, 1, true);
	component->activateBus (kAudio, kOutput, 0, true);
	component->setActive (true);
	processor->setProcessing (true);

	std::atomic<bool> running {true};
	std::thread audio ([&] {
		std::vector<float> inL (512), inR (512), outL (512), outR (512), side (512);
		float* sidePtr[2] = {side.data (), side.data ()};
		float* inPtr[2] = {inL.data (), inR.data ()};
		float* outPtr[2] = {outL.data (), outR.data ()};
		AudioBusBuffers buses[2] {};
		AudioBusBuffers& in = buses[0];
		AudioBusBuffers out {};
		in.numChannels = out.numChannels = 2;
		buses[1].numChannels = 2;
		buses[1].channelBuffers32 = sidePtr;
		in.channelBuffers32 = inPtr;
		out.channelBuffers32 = outPtr;
		ParameterChanges inChanges, outChanges;
		size_t pos = 0;
		auto next = std::chrono::steady_clock::now ();
		while (running)
		{
			for (int i = 0; i < 512; ++i, ++pos)
			{
				inL[i] = wavL[pos % wavL.size ()];
				inR[i] = wavR[pos % wavR.size ()];
				// A kick on the sidechain twice a second.
				const double beat = std::fmod (static_cast<double> (pos) / 44100.0, 0.5);
				side[i] = beat < 0.12 ? static_cast<float> (0.7 * std::sin (2.0 * 3.14159265358979 * 60.0 * beat)) : 0.f;
			}
			outChanges.clearQueue ();
			ProcessData data {};
			data.processMode = kRealtime;
			data.symbolicSampleSize = kSample32;
			data.numSamples = 512;
			data.numInputs = 2;
			data.numOutputs = 1;
			data.inputs = buses;
			data.outputs = &out;
			data.inputParameterChanges = &inChanges;
			data.outputParameterChanges = &outChanges;
			processor->process (data);
			next += std::chrono::microseconds (static_cast<long long> (512.0 / 44100.0 * 1e6));
			std::this_thread::sleep_until (next);
		}
	});

	WNDCLASSW wc {};
	wc.lpfnWndProc = windowProc;
	wc.hInstance = instance;
	wc.lpszClassName = L"LoopsMasterUiTest";
	RegisterClassW (&wc);

	IPlugView* view = controller->createView (ViewType::kEditor);
	check (view != nullptr, "Plugin provides a custom window", view != nullptr);
	if (view)
	{
		ViewRect size;
		view->getSize (&size);
		check (size.getWidth () == 780 && size.getHeight () == 516, "Window reports its 780 x 516 size", size.getWidth ());
		RECT frame {0, 0, size.getWidth (), size.getHeight ()};
		AdjustWindowRect (&frame, WS_OVERLAPPEDWINDOW, FALSE);
		window = CreateWindowW (L"LoopsMasterUiTest", L"LoopsMaster test", WS_OVERLAPPEDWINDOW, 40, 40, frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, instance, nullptr);
		ShowWindow (window, SW_SHOWNOACTIVATE);
		check (view->attached (window, kPlatformTypeHWND) == kResultOk, "Window attaches to the host", 1);
		plugWindow = FindWindowExW (window, nullptr, nullptr, nullptr);
		UpdateWindow (window);

		// Let audio and waves settle before the first picture, then one step every half second.
		SetTimer (window, 1, 1500, nullptr);
		MSG msg;
		bool fast = false;
		while (GetMessageW (&msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage (&msg);
			DispatchMessageW (&msg);
			if (msg.message == WM_TIMER && !fast)
			{
				fast = true;
				SetTimer (window, 1, 500, nullptr);
			}
		}
		view->removed ();
		view->release ();
		DestroyWindow (window);
	}

	running = false;
	audio.join ();
	processor->setProcessing (false);
	component->setActive (false);
	controller->setComponentHandler (nullptr);
	component->terminate ();
	std::fprintf (results, failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
	std::fclose (results);
	return failures ? 1 : 0;
}
