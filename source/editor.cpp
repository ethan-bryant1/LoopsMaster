// The LoopsMaster window, drawn in code: white on black, one typeface, thin lines.
//
// Sections run top to bottom. On the Layers page, three hold their controls and a live wave. All
// three waves share one time base, so the wave period visibly doubles from Body to Sub and again
// from Sub to Deep. The Master page goes down the same way in signal order, Duck to Limit, with the
// meters beside it.
#include "editor.h"
#include "logo_png.h"

#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include "vstgui/lib/platform/platformfactory.h"
#if SMTG_OS_WINDOWS
#include "vstgui/lib/platform/win32/win32factory.h"
#endif
#include "vstgui/vstgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace VSTGUI;

namespace loopsmaster {
namespace {

const CColor kBlack (0, 0, 0);
const CColor kWhite (255, 255, 255);
const CColor kDim (255, 255, 255, 170);
const CColor kLine (255, 255, 255, 56);
const CColor kFaint (255, 255, 255, 30);

const CCoord kWindowWidth = 780;
const CCoord kHeight = 516;
const CCoord kSurfaceHeight = 100;
const CCoord kBandHeight = 108;
const CCoord kBedrockTop = kSurfaceHeight + 3 * kBandHeight;
const CCoord kKnobWidth = 66;
const CCoord kKnobHeight = 96;
const CCoord kMasterBandHeight = 104;
const int32_t kPageTag = 9000;
const int32_t kEqBandTag = 9001;
const CCoord kEqGraphBottom = kSurfaceHeight + 256;

SharedPointer<CFontDesc> font (CCoord size, int32_t style = kNormalFace)
{
	return makeOwned<CFontDesc> ("Segoe UI", size, style);
}

SharedPointer<CFontDesc> lightFont (CCoord size)
{
	return makeOwned<CFontDesc> ("Segoe UI Light", size);
}

// One pixel, on the pixel grid so it stays sharp.
void hairline (CDrawContext* c, CCoord x0, CCoord y0, CCoord x1, CCoord y1)
{
	c->setFrameColor (kLine);
	c->setLineWidth (1);
	c->setLineStyle (kLineSolid);
	c->drawLine (CPoint (x0 + 0.5, y0 + 0.5), CPoint (x1 + 0.5, y1 + 0.5));
}

void outline (CDrawContext* c, const CRect& r, const CColor& colour)
{
	c->setFrameColor (colour);
	c->setLineWidth (1);
	c->setLineStyle (kLineSolid);
	c->drawRect (CRect (r.left + 0.5, r.top + 0.5, r.right - 0.5, r.bottom - 0.5), kDrawStroked);
}

using Formatter = std::function<std::string (float)>;

// A round control. Drag up or down, hold Shift for fine moves, scroll, or double-click to reset.
class Knob : public CControl
{
public:
	Knob (const CPoint& topLeft, IControlListener* listener, int32_t tag, const char* label_, float defaultNorm, Formatter format_)
	: CControl (CRect (topLeft, CPoint (kKnobWidth, kKnobHeight)), listener, tag), label (label_), format (std::move (format_))
	{
		setDefaultValue (defaultNorm);
		setMin (0.f);
		setMax (1.f);
	}

	bool isDragging () const { return dragging; }

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		const CCoord radius = 19;
		const CPoint centre (v.left + v.getWidth () / 2, v.top + 6 + radius);
		const CRect disc (centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius);
		const double start = 135.0, sweep = 270.0;
		const float value = getValueNormalized ();

		c->setLineStyle (kLineSolid);
		if (auto track = owned (c->createGraphicsPath ()))
		{
			track->addArc (disc, start, start + sweep, true);
			c->setFrameColor (kLine);
			c->setLineWidth (1.5);
			c->drawGraphicsPath (track, CDrawContext::kPathStroked);
		}
		if (value > 0.003f)
			if (auto arc = owned (c->createGraphicsPath ()))
			{
				arc->addArc (disc, start, start + sweep * value, true);
				c->setFrameColor (kWhite);
				c->setLineWidth (2);
				c->drawGraphicsPath (arc, CDrawContext::kPathStroked);
			}
		const double angle = (start + sweep * value) * 3.14159265358979 / 180.0;
		c->setFrameColor (kWhite);
		c->setLineWidth (1.5);
		c->drawLine (CPoint (centre.x + std::cos (angle) * 9, centre.y + std::sin (angle) * 9), CPoint (centre.x + std::cos (angle) * radius, centre.y + std::sin (angle) * radius));

		c->setFont (font (11));
		c->setFontColor (kDim);
		c->drawString (label.c_str (), CRect (v.left, v.top + 50, v.right, v.top + 66), kCenterText);
		c->setFont (font (12));
		c->setFontColor (kWhite);
		c->drawString (format (value).c_str (), CRect (v.left, v.top + 66, v.right, v.top + 84), kCenterText);
		setDirty (false);
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		beginEdit ();
		if (e.clickCount == 2)
		{
			setValueAndNotify (getDefaultValue ());
			endEdit ();
			e.consumed = true;
			e.ignoreFollowUpMoveAndUpEvents (true);
			return;
		}
		dragging = true;
		lastY = e.mousePosition.y;
		e.consumed = true;
	}

	void onMouseMoveEvent (MouseMoveEvent& e) override
	{
		if (!dragging)
			return;
		const float speed = e.modifiers.has (ModifierKey::Shift) ? 0.0006f : 0.005f;
		setValueAndNotify (getValueNormalized () + static_cast<float> (lastY - e.mousePosition.y) * speed);
		lastY = e.mousePosition.y;
		e.consumed = true;
	}

	void onMouseUpEvent (MouseUpEvent& e) override
	{
		if (!dragging)
			return;
		dragging = false;
		endEdit ();
		e.consumed = true;
	}

	void onMouseCancelEvent (MouseCancelEvent& e) override
	{
		if (dragging)
		{
			dragging = false;
			endEdit ();
		}
		e.consumed = true;
	}

	void onMouseWheelEvent (MouseWheelEvent& e) override
	{
		if (e.deltaY == 0.0)
			return;
		beginEdit ();
		setValueAndNotify (getValueNormalized () + static_cast<float> (e.deltaY) * 0.02f);
		endEdit ();
		e.consumed = true;
	}

	CLASS_METHODS (Knob, CControl)

private:
	void setValueAndNotify (float norm)
	{
		setValueNormalized (norm < 0.f ? 0.f : norm > 1.f ? 1.f : norm);
		valueChanged ();
		invalid ();
	}

	std::string label;
	Formatter format;
	bool dragging = false;
	CCoord lastY = 0;
};

// A row of mutually exclusive choices, used for the slope.
class Segments : public CControl
{
public:
	Segments (const CRect& size, IControlListener* listener, int32_t tag, std::vector<std::string> names_, bool textOnly_ = false)
	: CControl (size, listener, tag), names (std::move (names_)), textOnly (textOnly_)
	{
		setMin (0.f);
		setMax (1.f);
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		const int count = static_cast<int> (names.size ());
		const int selected = static_cast<int> (getValueNormalized () * (count - 1) + 0.5f);
		const CCoord w = v.getWidth () / count;
		const bool dimmed = getAlphaValue () < 1.f;
		c->setFont (font (textOnly ? 13 : 12));
		for (int i = 0; i < count; ++i)
		{
			const CRect r (v.left + i * w, v.top, v.left + (i + 1) * w, v.bottom);
			const bool chosen = i == selected;
			if (textOnly)
			{
				if (chosen)
				{
					c->setFillColor (kWhite);
					c->drawRect (CRect (r.left + 12, v.bottom - 5, r.right - 12, v.bottom - 4), kDrawFilled);
				}
				c->setFontColor (chosen ? kWhite : kDim);
			}
			else
			{
				if (chosen && !dimmed)
				{
					c->setFillColor (kWhite);
					c->drawRect (r, kDrawFilled);
				}
				else if (chosen)
					outline (c, r, kWhite);
				c->setFontColor (chosen && !dimmed ? kBlack : chosen ? kWhite : kDim);
			}
			c->drawString (names[i].c_str (), r, kCenterText);
		}
		if (!textOnly)
			outline (c, v, kLine);
		setDirty (false);
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		const int count = static_cast<int> (names.size ());
		int index = static_cast<int> ((e.mousePosition.x - getViewSize ().left) / (getViewSize ().getWidth () / count));
		index = index < 0 ? 0 : index >= count ? count - 1 : index;
		beginEdit ();
		setValueNormalized (static_cast<float> (index) / static_cast<float> (count - 1));
		valueChanged ();
		endEdit ();
		invalid ();
		e.consumed = true;
		e.ignoreFollowUpMoveAndUpEvents (true);
	}

	CLASS_METHODS (Segments, CControl)

private:
	std::vector<std::string> names;
	bool textOnly;
};

// Switches a whole stage in or out.
class StageSwitch : public CControl
{
public:
	StageSwitch (const CRect& size, IControlListener* listener, int32_t tag, const char* onText_ = "On", const char* offText_ = "Off")
	: CControl (size, listener, tag), onText (onText_), offText (offText_)
	{
		setMin (0.f);
		setMax (1.f);
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		const bool on = getValueNormalized () >= 0.5f;
		const bool solid = on && getAlphaValue () >= 1.f;
		if (solid)
		{
			c->setFillColor (kWhite);
			c->drawRect (v, kDrawFilled);
		}
		else
			outline (c, v, on ? kWhite : kLine);
		c->setFont (font (12));
		c->setFontColor (solid ? kBlack : on ? kWhite : kDim);
		c->drawString (on ? onText : offText, v, kCenterText);
		setDirty (false);
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		beginEdit ();
		setValueNormalized (getValueNormalized () >= 0.5f ? 0.f : 1.f);
		valueChanged ();
		endEdit ();
		invalid ();
		e.consumed = true;
		e.ignoreFollowUpMoveAndUpEvents (true);
	}

	CLASS_METHODS (StageSwitch, CControl)

private:
	const char* onText;
	const char* offText;
};

class BypassButton : public CControl
{
public:
	BypassButton (const CRect& size, IControlListener* listener, int32_t tag) : CControl (size, listener, tag)
	{
		setMin (0.f);
		setMax (1.f);
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		const bool on = getValueNormalized () >= 0.5f;
		if (on)
		{
			c->setFillColor (kWhite);
			c->drawRect (v, kDrawFilled);
		}
		else
			outline (c, v, kLine);
		c->setFont (font (12));
		c->setFontColor (on ? kBlack : kWhite);
		c->drawString (on ? "Bypassed" : "Bypass", v, kCenterText);
		setDirty (false);
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		beginEdit ();
		setValueNormalized (getValueNormalized () >= 0.5f ? 0.f : 1.f);
		valueChanged ();
		endEdit ();
		invalid ();
		e.consumed = true;
		e.ignoreFollowUpMoveAndUpEvents (true);
	}

	CLASS_METHODS (BypassButton, CControl)
};

// The header with the logo, the Layers sections and their fixed text. Controls sit on top of it.
class Chart : public CView
{
public:
	explicit Chart (const CRect& size) : CView (size)
	{
		setMouseEnabled (false);
		// VSTGUI's image decoder comes from COM. If the host hadn't started COM when it loaded the
		// plugin there is no decoder, and asking for an image would crash, so the name is drawn instead.
		bool canDecode = true;
#if SMTG_OS_WINDOWS
		const auto* win32 = getPlatformFactory ().asWin32Factory ();
		canDecode = win32 && win32->getWICImagingFactory ();
#endif
		if (canDecode)
			if (auto bitmap = getPlatformFactory ().createBitmapFromMemory (kLogoPng, sizeof (kLogoPng)))
			{
				bitmap->setScaleFactor (2.0);
				logo = makeOwned<CBitmap> (bitmap);
			}
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		c->setFillColor (kBlack);
		c->drawRect (getViewSize (), kDrawFilled);

		const CCoord logoTop = (kSurfaceHeight - kLogoHeight / 2.0) / 2;
		if (logo)
		{
			c->setBitmapInterpolationQuality (BitmapInterpolationQuality::kHigh);
			c->drawBitmap (logo, CRect (18, logoTop, 18 + kLogoWidth / 2.0, logoTop + kLogoHeight / 2.0));
		}
		else
		{
			c->setFont (lightFont (28));
			c->setFontColor (kWhite);
			c->drawString ("Loops", CRect (18, 0, 140, kSurfaceHeight), kLeftText);
		}
		hairline (c, 18, kSurfaceHeight - 1, kWindowWidth - 18, kSurfaceHeight - 1);

		const char* titles[] = {"Body", "Sub", "Deep"};
		const char* notes[] = {"The note itself", "One octave down", "Two octaves down"};
		for (int i = 0; i < 3; ++i)
		{
			const CCoord top = kSurfaceHeight + i * kBandHeight;
			if (i > 0)
				hairline (c, 18, top, kWindowWidth - 18, top);
			c->setFont (lightFont (20));
			c->setFontColor (kWhite);
			c->drawString (titles[i], CRect (18, top + 12, 130, top + 40), kLeftText);
			c->setFont (font (11));
			c->setFontColor (kDim);
			c->drawString (notes[i], CRect (18, top + 40, 130, top + 58), kLeftText);
		}

		hairline (c, 18, kBedrockTop, kWindowWidth - 18, kBedrockTop);
		c->setFont (font (11));
		c->setFontColor (kDim);
		c->drawString ("Slope, dB per octave", CRect (98, kBedrockTop + 17, 220, kBedrockTop + 35), kLeftText);
		setDirty (false);
	}

	CLASS_METHODS (Chart, CView)

private:
	SharedPointer<CBitmap> logo;
};

// The Master page's bands. It covers the Layers bands while that page is hidden.
class MasterChart : public CView
{
public:
	// With plainFloor set it paints just the plain ground the EQ's controls sit on.
	explicit MasterChart (const CRect& size, bool plainFloor_ = false) : CView (size), plainFloor (plainFloor_) { setMouseEnabled (false); }

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		c->setFillColor (kBlack);
		c->drawRect (getViewSize (), kDrawFilled);
		if (plainFloor)
		{
			hairline (c, 18, kEqGraphBottom, kWindowWidth - 18, kEqGraphBottom);
			setDirty (false);
			return;
		}
		const char* titles[] = {"Duck", "Shape", "Clip", "Limit"};
		const char* notes[] = {"Lows, with the kick", "Width, tone, squash", "Heat, then clip", "Final ceiling"};
		for (int i = 0; i < 4; ++i)
		{
			const CCoord top = kSurfaceHeight + i * kMasterBandHeight;
			if (i > 0)
				hairline (c, 18, top, 530, top);
			c->setFont (lightFont (20));
			c->setFontColor (kWhite);
			c->drawString (titles[i], CRect (18, top + 12, 130, top + 40), kLeftText);
			c->setFont (font (11));
			c->setFontColor (kDim);
			c->drawString (notes[i], CRect (18, top + 40, 132, top + 58), kLeftText);
		}
		hairline (c, 534, kSurfaceHeight + 12, 534, kHeight - 12);
		setDirty (false);
	}

	CLASS_METHODS (MasterChart, CView)

private:
	bool plainFloor;
};

// Says whether a kick is reaching the sidechain input.
class SidechainStatus : public CView
{
public:
	SidechainStatus (const CRect& size, MeterData& meters_) : CView (size), meters (meters_)
	{
		setMouseEnabled (false);
		setTransparency (true);
	}

	void refresh (bool duckOn_, bool followsBeat_)
	{
		if (followsBeat_ != followsBeat)
		{
			followsBeat = followsBeat_;
			invalid ();
		}
		if (meters.sidechainPeak.exchange (0.f, std::memory_order_relaxed) > 0.001f)
			framesLeft = 45;
		else if (framesLeft > 0)
			--framesLeft;
		const bool now = framesLeft > 0;
		if (now != receiving || duckOn_ != duckOn)
		{
			receiving = now;
			duckOn = duckOn_;
			invalid ();
		}
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		c->setFont (font (12));
		c->setFontColor (kWhite);
		c->drawString (followsBeat ? "Host tempo" : "Sidechain input", CRect (v.left, v.top, v.right, v.top + 18), kLeftText);
		c->setFontColor (followsBeat || receiving || duckOn ? kWhite : kDim);
		c->drawString (followsBeat ? "Ducks on the beat" : receiving ? "Signal coming in" : duckOn ? "No signal, not ducking" : "No signal yet", CRect (v.left, v.top + 18, v.right, v.top + 36), kLeftText);
		setDirty (false);
	}

	CLASS_METHODS (SidechainStatus, CView)

private:
	MeterData& meters;
	int framesLeft = 0;
	bool receiving = false;
	bool duckOn = false;
	bool followsBeat = false;
};

// Levels, gain reduction and loudness. Click to restart the integrated and true-peak readings.
class MeterPanel : public CView
{
public:
	MeterPanel (const CRect& size, MeterData& meters_) : CView (size), meters (meters_) {}

	void refresh ()
	{
		const auto level = [] (std::atomic<float>& a) {
			const float peak = a.exchange (0.f, std::memory_order_relaxed);
			return peak > 0.001f ? 20.f * std::log10 (peak) : -60.f;
		};
		inDb = std::max (level (meters.inPeak), inDb - 1.2f);
		outDb = std::max (level (meters.outPeak), outDb - 1.2f);
		duckDb = std::max (meters.duckDb.exchange (0.f, std::memory_order_relaxed), duckDb - 0.8f);
		clipDb = std::max (meters.clipDb.exchange (0.f, std::memory_order_relaxed), clipDb - 0.8f);
		limitDb = std::max (meters.limitDb.exchange (0.f, std::memory_order_relaxed), limitDb - 0.8f);
		invalid ();
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		c->setFillColor (kBlack);
		c->drawRect (v, kDrawFilled);

		// Levels rise from the bottom. Gain being taken away hangs from the top, in grey.
		const CCoord barTop = v.top + 30;
		const CCoord barBottom = v.top + 222;
		const CCoord barWidth = 8;
		const CCoord column = (v.getWidth () - 24) / 5;
		const char* names[] = {"In", "Duck", "Clip", "Limit", "Out"};
		const float values[] = {inDb, duckDb, clipDb, limitDb, outDb};
		c->setFont (font (11));
		for (int i = 0; i < 5; ++i)
		{
			const CCoord cell = v.left + 12 + i * column;
			const CCoord x = std::floor (cell + (column - barWidth) / 2);
			c->setFillColor (kFaint);
			c->drawRect (CRect (x, barTop, x + barWidth, barBottom), kDrawFilled);
			const bool reduction = i >= 1 && i <= 3;
			if (reduction)
			{
				const CCoord h = (barBottom - barTop) * std::min (values[i] / 18.f, 1.f);
				c->setFillColor (kDim);
				c->drawRect (CRect (x, barTop, x + barWidth, barTop + h), kDrawFilled);
			}
			else
			{
				const CCoord h = (barBottom - barTop) * std::clamp (1.f + values[i] / 36.f, 0.f, 1.f);
				c->setFillColor (kWhite);
				c->drawRect (CRect (x, barBottom - h, x + barWidth, barBottom), kDrawFilled);
			}
			c->setFontColor (kDim);
			c->drawString (names[i], CRect (cell, v.top + 8, cell + column, v.top + 26), kCenterText);
			char text[16];
			if (reduction)
				std::snprintf (text, sizeof text, values[i] < 0.05f ? "0" : "-%.1f", values[i]);
			else if (values[i] <= -59.f)
				std::snprintf (text, sizeof text, "--");
			else
				std::snprintf (text, sizeof text, "%.1f", std::fabs (values[i]) < 0.05f ? 0.f : values[i]);
			// Over 0 dB reads as an inverted number.
			const CRect textBox (cell + 4, barBottom + 4, cell + column - 4, barBottom + 21);
			const bool over = !reduction && values[i] > 0.f;
			if (over)
			{
				c->setFillColor (kWhite);
				c->drawRect (textBox, kDrawFilled);
			}
			c->setFontColor (over ? kBlack : kWhite);
			c->drawString (text, textBox, kCenterText);
		}

		const float tp = meters.truePeakDb.load (std::memory_order_relaxed);
		row (c, 0, "Momentary", lufs (meters.momentary.load (std::memory_order_relaxed)), false);
		row (c, 1, "Short term", lufs (meters.shortTerm.load (std::memory_order_relaxed)), false);
		row (c, 2, "Integrated", lufs (meters.integrated.load (std::memory_order_relaxed)), false);
		char text[32];
		if (tp <= -99.f)
			std::snprintf (text, sizeof text, "--");
		else
			std::snprintf (text, sizeof text, "%.1f dBTP", std::fabs (tp) < 0.05f ? 0.f : tp);
		row (c, 3, "True peak", text, tp > 0.f);
		std::snprintf (text, sizeof text, "%+.2f", meters.correlation.load (std::memory_order_relaxed));
		row (c, 4, "Correlation", text, false);

		std::snprintf (text, sizeof text, "Click to reset. Latency %.1f ms", meters.latencyMs.load (std::memory_order_relaxed));
		c->setFont (font (11));
		c->setFontColor (kDim);
		c->drawString (text, CRect (v.left + 12, v.bottom - 22, v.right - 8, v.bottom - 4), kLeftText);
		setDirty (false);
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		meters.resetRequest.store (true, std::memory_order_relaxed);
		e.consumed = true;
		e.ignoreFollowUpMoveAndUpEvents (true);
	}

	CLASS_METHODS (MeterPanel, CView)

private:
	static std::string lufs (float value)
	{
		if (value <= -99.f)
			return "--";
		char text[24];
		std::snprintf (text, sizeof text, "%.1f LUFS", value);
		return text;
	}

	void row (CDrawContext* c, int index, const char* name, const std::string& value, bool warn)
	{
		const CRect v = getViewSize ();
		const CCoord top = v.top + 250 + index * 24;
		c->setFont (font (12));
		c->setFontColor (kDim);
		c->drawString (name, CRect (v.left + 12, top, v.left + 100, top + 22), kLeftText);
		const CRect box (v.left + 90, top, v.right - 12, top + 22);
		if (warn)
		{
			c->setFillColor (kWhite);
			c->drawRect (box, kDrawFilled);
		}
		c->setFontColor (warn ? kBlack : kWhite);
		c->drawString (value.c_str (), warn ? CRect (box.left, box.top, box.right - 4, box.bottom) : box, kRightText);
	}

	MeterData& meters;
	float inDb = -60.f, outDb = -60.f, duckDb = 0.f, clipDb = 0.f, limitDb = 0.f;
};

// The EQ's curve over a live spectrum of the output. Drag a numbered point to move its band,
// scroll to change its Q, and double-click to flatten it.
class EqGraph : public CView
{
public:
	EqGraph (const CRect& size, EditorModel* model_, Steinberg::Vst::EditController* controller_, std::function<void (int)> onSelect_)
	: CView (size), model (model_), controller (controller_), onSelect (std::move (onSelect_))
	{
		std::fill (std::begin (bars), std::end (bars), -96.f);
	}

	void select (int band)
	{
		selected = band;
		invalid ();
	}

	void refresh ()
	{
		MeterData& m = model->meterData ();
		const uint32_t size = MeterData::kSpectrumSize;
		const uint32_t end = m.spectrumWritten.load (std::memory_order_relaxed);
		std::vector<float> re (size), im (size, 0.f);
		float windowSum = 0.f;
		for (uint32_t i = 0; i < size; ++i)
		{
			const float w = 0.5f - 0.5f * std::cos (6.2831853f * static_cast<float> (i) / static_cast<float> (size - 1));
			re[i] = w * m.spectrum[(end - size + i) & (size - 1)].load (std::memory_order_relaxed);
			windowSum += w;
		}
		transform (re, im);
		const double sr = m.sampleRate.load (std::memory_order_relaxed);
		const CCoord width = getViewSize ().getWidth ();
		for (int b = 0; b < kBars; ++b)
		{
			const double from = hzAt (width * b / kBars);
			const double to = hzAt (width * (b + 1) / kBars);
			const uint32_t a = std::clamp (static_cast<uint32_t> (from * size / sr), 1u, size / 2 - 1);
			const uint32_t z = std::clamp (static_cast<uint32_t> (to * size / sr), a, size / 2 - 1);
			float most = 0.f;
			for (uint32_t k = a; k <= z; ++k)
				most = std::max (most, re[k] * re[k] + im[k] * im[k]);
			const float db = 10.f * std::log10 (std::max (4.f * most / (windowSum * windowSum), 1e-12f));
			bars[b] = std::max (db, bars[b] - 1.5f);
		}
		invalid ();
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		c->setFillColor (kBlack);
		c->drawRect (v, kDrawFilled);

		c->setLineWidth (1);
		c->setLineStyle (kLineSolid);
		c->setFrameColor (kFaint);
		c->setFont (font (11));
		c->setFontColor (kDim);
		for (double hz : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0})
		{
			const CCoord x = xAt (hz);
			c->drawLine (CPoint (x, v.top), CPoint (x, v.bottom));
			char text[16];
			std::snprintf (text, sizeof text, hz >= 1000.0 ? "%.0fk" : "%.0f", hz >= 1000.0 ? hz / 1000.0 : hz);
			c->drawString (text, CRect (x + 3, v.bottom - 18, x + 40, v.bottom - 2), kLeftText);
		}
		for (double db : {-12.0, -6.0, 0.0, 6.0, 12.0})
			c->drawLine (CPoint (v.left, yAt (db)), CPoint (v.right, yAt (db)));

		if (auto path = owned (c->createGraphicsPath ()))
		{
			path->beginSubpath (v.left, v.bottom);
			for (int b = 0; b < kBars; ++b)
			{
				const CCoord x = v.left + v.getWidth () * (b + 0.5) / kBars;
				const CCoord y = v.bottom - v.getHeight () * std::clamp ((bars[b] + 84.f) / 84.f, 0.f, 1.f);
				path->addLine (x, y);
			}
			path->addLine (v.right, v.bottom);
			path->closeSubpath ();
			c->setFillColor (CColor (255, 255, 255, 34));
			c->drawGraphicsPath (path, CDrawContext::kPathFilled);
		}

		const double sr = model->meterData ().sampleRate.load (std::memory_order_relaxed);
		Biquad filters[5][kEqMaxStages];
		int stages[5];
		bool active[5];
		for (int b = 0; b < 5; ++b)
		{
			EqBandSettings band;
			band.type = eqTypeFromNorm (norm (b, 0));
			band.hz = eqHzFromNorm (norm (b, 1));
			band.gainDb = eqGainFromNorm (norm (b, 2));
			band.q = eqQFromNorm (norm (b, 3));
			band.slope = static_cast<int> (model->currentValue (kEqSlopeFirst + b) * 2.0 + 0.5);
			active[b] = eqBandActive (band.type, std::fabs (band.gainDb) < 0.01 ? 0.0 : band.gainDb);
			stages[b] = setEqBand (filters[b], band, sr);
		}
		if (auto path = owned (c->createGraphicsPath ()))
		{
			for (int px = 0; px <= static_cast<int> (v.getWidth ()); px += 2)
			{
				double total = 0.0;
				for (int b = 0; b < 5; ++b)
					if (active[b])
						for (int i = 0; i < stages[b]; ++i)
							total += filters[b][i].magnitudeDb (hzAt (px), sr);
				const CCoord y = std::clamp (yAt (total), v.top, v.bottom);
				if (px == 0)
					path->beginSubpath (v.left + px, y);
				else
					path->addLine (v.left + px, y);
			}
			c->setFrameColor (kWhite);
			c->setLineWidth (1.6);
			c->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
			c->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}

		c->setFont (font (11));
		for (int b = 0; b < 5; ++b)
		{
			// Filled is the selected band, outlined is in use, faint is flat.
			const CPoint p = nodeAt (b);
			const CRect dot (p.x - 9, p.y - 9, p.x + 9, p.y + 9);
			c->setFillColor (b == selected ? kWhite : kBlack);
			c->drawEllipse (dot, kDrawFilled);
			c->setFrameColor (b == selected || active[b] ? kWhite : kLine);
			c->setLineWidth (1);
			c->setLineStyle (kLineSolid);
			c->drawEllipse (dot, kDrawStroked);
			char text[4];
			std::snprintf (text, sizeof text, "%d", b + 1);
			c->setFontColor (b == selected ? kBlack : active[b] ? kWhite : kDim);
			c->drawString (text, dot, kCenterText);
		}
		setDirty (false);
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		int nearest = -1;
		double best = 16.0 * 16.0;
		for (int b = 0; b < 5; ++b)
		{
			const CPoint p = nodeAt (b);
			const double d = (p.x - e.mousePosition.x) * (p.x - e.mousePosition.x) + (p.y - e.mousePosition.y) * (p.y - e.mousePosition.y);
			if (d < best)
			{
				best = d;
				nearest = b;
			}
		}
		e.consumed = true;
		if (nearest < 0)
		{
			e.ignoreFollowUpMoveAndUpEvents (true);
			return;
		}
		selected = nearest;
		onSelect (nearest);
		if (e.clickCount == 2)
		{
			set (kEqFirst + nearest * 4 + 2, 0.5, true);
			e.ignoreFollowUpMoveAndUpEvents (true);
			return;
		}
		dragging = true;
		controller->beginEdit (tag (selected, 1));
		controller->beginEdit (tag (selected, 2));
	}

	void onMouseMoveEvent (MouseMoveEvent& e) override
	{
		if (!dragging)
			return;
		const CRect v = getViewSize ();
		set (tag (selected, 1), std::clamp (eqHzToNorm (hzAt (e.mousePosition.x - v.left)), 0.0, 1.0), false);
		const double gain = 18.0 * (v.top + v.getHeight () / 2 - e.mousePosition.y) / (v.getHeight () / 2);
		set (tag (selected, 2), std::clamp (eqGainToNorm (gain), 0.0, 1.0), false);
		e.consumed = true;
		invalid ();
	}

	void onMouseUpEvent (MouseUpEvent& e) override
	{
		if (!dragging)
			return;
		dragging = false;
		controller->endEdit (tag (selected, 1));
		controller->endEdit (tag (selected, 2));
		e.consumed = true;
	}

	void onMouseCancelEvent (MouseCancelEvent& e) override
	{
		if (dragging)
		{
			dragging = false;
			controller->endEdit (tag (selected, 1));
			controller->endEdit (tag (selected, 2));
		}
		e.consumed = true;
	}

	void onMouseWheelEvent (MouseWheelEvent& e) override
	{
		if (e.deltaY == 0.0)
			return;
		set (tag (selected, 3), std::clamp (norm (selected, 3) + e.deltaY * 0.03, 0.0, 1.0), true);
		e.consumed = true;
		invalid ();
	}

	CLASS_METHODS (EqGraph, CView)

private:
	static constexpr int kBars = 156;

	static int tag (int band, int field) { return kEqFirst + band * 4 + field; }
	double norm (int band, int field) const { return model->currentValue (tag (band, field)); }

	void set (int param, double value, bool wrap)
	{
		if (wrap)
			controller->beginEdit (param);
		controller->setParamNormalized (param, value);
		controller->performEdit (param, value);
		if (wrap)
			controller->endEdit (param);
	}

	double hzAt (CCoord px) const { return 20.0 * std::pow (1000.0, px / getViewSize ().getWidth ()); }
	CCoord xAt (double hz) const { return getViewSize ().left + getViewSize ().getWidth () * eqHzToNorm (hz); }
	CCoord yAt (double db) const
	{
		const CRect v = getViewSize ();
		return v.top + v.getHeight () / 2 - db / 18.0 * (v.getHeight () / 2);
	}
	CPoint nodeAt (int band) const
	{
		const int type = eqTypeFromNorm (norm (band, 0));
		return CPoint (xAt (eqHzFromNorm (norm (band, 1))), yAt (eqIsGainless (type) ? 0.0 : eqGainFromNorm (norm (band, 2))));
	}

	// A plain in-place radix-2 FFT.
	static void transform (std::vector<float>& re, std::vector<float>& im)
	{
		const size_t n = re.size ();
		for (size_t i = 1, j = 0; i < n; ++i)
		{
			size_t bit = n >> 1;
			for (; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if (i < j)
			{
				std::swap (re[i], re[j]);
				std::swap (im[i], im[j]);
			}
		}
		for (size_t len = 2; len <= n; len <<= 1)
		{
			const double angle = -6.283185307179586 / static_cast<double> (len);
			for (size_t i = 0; i < n; i += len)
				for (size_t k = 0; k < len / 2; ++k)
				{
					const float wr = static_cast<float> (std::cos (angle * static_cast<double> (k)));
					const float wi = static_cast<float> (std::sin (angle * static_cast<double> (k)));
					const size_t a = i + k, b = i + k + len / 2;
					const float tr = re[b] * wr - im[b] * wi;
					const float ti = re[b] * wi + im[b] * wr;
					re[b] = re[a] - tr;
					im[b] = im[a] - ti;
					re[a] += tr;
					im[a] += ti;
				}
		}
	}

	EditorModel* model;
	Steinberg::Vst::EditController* controller;
	std::function<void (int)> onSelect;
	int selected = 0;
	bool dragging = false;
	float bars[kBars];
};

// The Multi page's bands.
class MultiChart : public CView
{
public:
	explicit MultiChart (const CRect& size) : CView (size) { setMouseEnabled (false); }

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		c->setFillColor (kBlack);
		c->drawRect (getViewSize (), kDrawFilled);
		const CCoord tops[] = {kSurfaceHeight, kSurfaceHeight + kMasterBandHeight, kSurfaceHeight + 2 * kMasterBandHeight};
		const char* titles[] = {"Squash", "Bands", "Action"};
		const char* notes[] = {"Amount and speed", "Splits and levels", "Listen to one band"};
		for (int i = 0; i < 3; ++i)
		{
			if (i > 0)
				hairline (c, 18, tops[i], 530, tops[i]);
			c->setFont (lightFont (20));
			c->setFontColor (kWhite);
			c->drawString (titles[i], CRect (18, tops[i] + 12, 130, tops[i] + 40), kLeftText);
			c->setFont (font (11));
			c->setFontColor (kDim);
			c->drawString (notes[i], CRect (18, tops[i] + 40, 132, tops[i] + 58), kLeftText);
		}
		hairline (c, 534, kSurfaceHeight + 12, 534, kHeight - 12);
		setDirty (false);
	}

	CLASS_METHODS (MultiChart, CView)
};

// Live gain per band: up to the right of centre, down to the left.
class BandMeters : public CView
{
public:
	BandMeters (const CRect& size, MeterData& meters_) : CView (size), meters (meters_)
	{
		setMouseEnabled (false);
		setTransparency (true);
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		const char* names[] = {"Low", "Mid", "High"};
		const CCoord rowHeight = v.getHeight () / 3;
		const CCoord left = v.left + 44, right = v.right - 58;
		const CCoord centre = (left + right) / 2;
		for (int b = 0; b < 3; ++b)
		{
			const float db = meters.squashDb[b].load (std::memory_order_relaxed);
			const CCoord top = v.top + b * rowHeight + 18, bottom = v.top + (b + 1) * rowHeight - 18;
			const CCoord middle = std::floor ((top + bottom) / 2);
			c->setFillColor (kFaint);
			c->drawRect (CRect (left, middle - 2, right, middle + 2), kDrawFilled);
			c->setFillColor (kWhite);
			c->drawRect (CRect (centre, middle - 6, centre + 1, middle + 6), kDrawFilled);
			const CCoord reach = (right - left) / 2 * std::clamp (db / 18.f, -1.f, 1.f);
			c->setFillColor (db < 0.f ? kDim : kWhite);
			c->drawRect (CRect (std::min (centre, centre + reach), middle - 2, std::max (centre, centre + reach), middle + 2), kDrawFilled);
			c->setFont (font (12));
			c->setFontColor (kDim);
			c->drawString (names[b], CRect (v.left, top, left - 6, bottom), kLeftText);
			char text[16];
			if (std::fabs (db) < 0.05f)
				std::snprintf (text, sizeof text, "0.0 dB");
			else
				std::snprintf (text, sizeof text, "%+.1f dB", db);
			c->setFontColor (kWhite);
			c->drawString (text, CRect (right + 4, top, v.right, bottom), kRightText);
		}
		setDirty (false);
	}

	CLASS_METHODS (BandMeters, CView)

private:
	MeterData& meters;
};

// A minute of short-term loudness, with the current readings under it. Click to reset.
class LoudnessHistory : public CView
{
public:
	LoudnessHistory (const CRect& size, MeterData& meters_) : CView (size), meters (meters_)
	{
		std::fill (std::begin (history), std::end (history), -100.f);
	}

	void sample ()
	{
		if (++frames < 6)
			return;
		frames = 0;
		history[at] = meters.shortTerm.load (std::memory_order_relaxed);
		at = (at + 1) % kPoints;
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		c->setFillColor (kBlack);
		c->drawRect (v, kDrawFilled);
		c->setFont (font (12));
		c->setFontColor (kDim);
		c->drawString ("Short term, last minute", CRect (v.left + 12, v.top + 8, v.right - 8, v.top + 28), kLeftText);

		const CRect plot (v.left + 12, v.top + 34, v.right - 12, v.top + 270);
		const auto yOf = [&] (float lufs) { return plot.bottom - plot.getHeight () * std::clamp ((lufs + 36.f) / 36.f, 0.f, 1.f); };
		c->setLineWidth (1);
		c->setLineStyle (kLineSolid);
		c->setFrameColor (kFaint);
		c->setFont (font (11));
		c->setFontColor (kDim);
		for (float lufs : {-6.f, -12.f, -18.f, -24.f, -30.f})
		{
			c->drawLine (CPoint (plot.left, yOf (lufs)), CPoint (plot.right, yOf (lufs)));
			char text[8];
			std::snprintf (text, sizeof text, "%.0f", lufs);
			c->drawString (text, CRect (plot.left + 3, yOf (lufs), plot.left + 40, yOf (lufs) + 16), kLeftText);
		}
		if (auto path = owned (c->createGraphicsPath ()))
		{
			bool started = false;
			for (int i = 0; i < kPoints; ++i)
			{
				const float lufs = history[(at + i) % kPoints];
				const CCoord x = plot.left + plot.getWidth () * i / (kPoints - 1);
				if (lufs <= -99.f)
				{
					started = false;
					continue;
				}
				if (started)
					path->addLine (x, yOf (lufs));
				else
					path->beginSubpath (x, yOf (lufs));
				started = true;
			}
			c->setFrameColor (kWhite);
			c->setLineWidth (1.4);
			c->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}

		const auto number = [] (float value, const char* units) {
			char text[24];
			if (value <= -99.f || value < -1e6f)
				std::snprintf (text, sizeof text, "--");
			else
				std::snprintf (text, sizeof text, "%.1f %s", value, units);
			return std::string (text);
		};
		const float range = meters.loudnessRange.load (std::memory_order_relaxed);
		row (c, 0, "Short term", number (meters.shortTerm.load (std::memory_order_relaxed), "LUFS"));
		row (c, 1, "Integrated", number (meters.integrated.load (std::memory_order_relaxed), "LUFS"));
		row (c, 2, "Range", range < 0.f ? "--" : number (range, "LU"));
		c->setFont (font (11));
		c->setFontColor (kDim);
		c->drawString ("Click to reset", CRect (v.left + 12, v.bottom - 22, v.right - 8, v.bottom - 4), kLeftText);
		setDirty (false);
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		meters.resetRequest.store (true, std::memory_order_relaxed);
		std::fill (std::begin (history), std::end (history), -100.f);
		e.consumed = true;
		e.ignoreFollowUpMoveAndUpEvents (true);
	}

	CLASS_METHODS (LoudnessHistory, CView)

private:
	static constexpr int kPoints = 300;

	void row (CDrawContext* c, int index, const char* name, const std::string& value)
	{
		const CRect v = getViewSize ();
		const CCoord top = v.top + 282 + index * 24;
		c->setFont (font (12));
		c->setFontColor (kDim);
		c->drawString (name, CRect (v.left + 12, top, v.left + 100, top + 22), kLeftText);
		c->setFontColor (kWhite);
		c->drawString (value.c_str (), CRect (v.left + 90, top, v.right - 12, top + 22), kRightText);
	}

	MeterData& meters;
	float history[kPoints];
	int at = 0, frames = 0;
};

// The stretch of time and the display gain that all three waves share. Sharing them keeps the
// waves lined up and keeps their sizes honest relative to each other.
struct WaveWindow
{
	static constexpr uint32_t kPoints = 440; // a little over 0.2 seconds
	uint32_t first = 0;
	float gain = 1.f;

	void update (const ScopeData& scope)
	{
		// End at the latest Deep cycle so the waves stand still. If none finished recently,
		// show the newest points instead of holding old audio on screen.
		const uint32_t written = scope.written.load (std::memory_order_relaxed);
		const uint32_t cycle = scope.lastDeepCycle.load (std::memory_order_relaxed);
		const uint32_t end = written - cycle < 200 ? cycle : written;
		first = end - kPoints;

		float peak = 0.f;
		for (uint32_t i = 0; i < kPoints; ++i)
		{
			const uint32_t at = (first + i) & (ScopeData::kSize - 1);
			peak = std::max ({peak, std::fabs (scope.body[at].load (std::memory_order_relaxed)),
			                  std::fabs (scope.sub[at].load (std::memory_order_relaxed)),
			                  std::fabs (scope.deep[at].load (std::memory_order_relaxed))});
		}
		// Zoom in on quiet signals, up to 8x. Shrink at once on peaks and grow back slowly.
		const float wanted = peak > 0.01f ? std::min (8.f, 0.95f / peak) : gain;
		gain = wanted < gain ? wanted : std::min (wanted, gain * 1.03f);
	}
};

// One band's live wave.
class Wave : public CView
{
public:
	using Samples = const std::atomic<float>*;

	Wave (const CRect& size, const WaveWindow& window_, Samples samples_) : CView (size), window (window_), samples (samples_)
	{
		setMouseEnabled (false);
		setTransparency (true);
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		const CCoord middle = v.top + v.getHeight () / 2;
		c->setFrameColor (kFaint);
		c->setLineWidth (1);
		c->setLineStyle (kLineSolid);
		c->drawLine (CPoint (v.left, middle), CPoint (v.right, middle));

		const uint32_t kPoints = WaveWindow::kPoints;
		if (auto path = owned (c->createGraphicsPath ()))
		{
			const CCoord scale = v.getHeight () * 0.48;
			for (uint32_t i = 0; i < kPoints; ++i)
			{
				float s = window.gain * samples[(window.first + i) & (ScopeData::kSize - 1)].load (std::memory_order_relaxed);
				s = s > 1.f ? 1.f : s < -1.f ? -1.f : s;
				const CCoord x = v.left + v.getWidth () * i / (kPoints - 1);
				const CCoord y = middle - s * scale;
				if (i == 0)
					path->beginSubpath (x, y);
				else
					path->addLine (x, y);
			}
			c->setFrameColor (kWhite);
			c->setLineWidth (1.3);
			c->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
			c->drawGraphicsPath (path, CDrawContext::kPathStroked);
		}
		setDirty (false);
	}

	CLASS_METHODS (Wave, CView)

private:
	const WaveWindow& window;
	Samples samples;
};

// The preset list, with an arrow showing that it opens.
class PresetMenu : public COptionMenu
{
public:
	PresetMenu (const CRect& size, IControlListener* listener, int32_t tag) : COptionMenu (size, listener, tag) {}

	void draw (CDrawContext* c) override
	{
		COptionMenu::draw (c);
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect v = getViewSize ();
		const CCoord x = v.right - 18;
		const CCoord y = v.top + v.getHeight () / 2;
		if (auto path = owned (c->createGraphicsPath ()))
		{
			path->beginSubpath (x - 5, y - 3);
			path->addLine (x + 5, y - 3);
			path->addLine (x, y + 3);
			path->closeSubpath ();
			c->setFillColor (kWhite);
			c->drawGraphicsPath (path, CDrawContext::kPathFilled);
		}
		setDirty (false);
	}

	CLASS_METHODS (PresetMenu, COptionMenu)
};

// Shows the note the sub layers are following.
class NoteReadout : public CView
{
public:
	NoteReadout (const CRect& size, const ScopeData& scope_) : CView (size), scope (scope_)
	{
		setMouseEnabled (false);
		setTransparency (true);
	}

	void refresh ()
	{
		const std::string next = describe (scope.noteHz.load (std::memory_order_relaxed));
		if (next != text)
		{
			text = next;
			invalid ();
		}
	}

	void draw (CDrawContext* c) override
	{
		c->setDrawMode (kAntiAliasing | kNonIntegralMode);
		c->setFont (font (12));
		c->setFontColor (kDim);
		c->drawString (text.c_str (), getViewSize (), kRightText);
		setDirty (false);
	}

	CLASS_METHODS (NoteReadout, CView)

private:
	static std::string describe (float hz)
	{
		if (hz <= 0.f)
			return "No note";
		static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		const int midi = static_cast<int> (std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
		char out[48];
		std::snprintf (out, sizeof out, "%s%d, %.0f Hz", names[((midi % 12) + 12) % 12], midi / 12 - 1, hz);
		return out;
	}

	const ScopeData& scope;
	std::string text = "No note";
};

class Editor : public Steinberg::Vst::VSTGUIEditor, public IControlListener
{
public:
	Editor (Steinberg::Vst::EditController* controller, EditorModel* model_, Steinberg::ViewRect* rect)
	: VSTGUIEditor (controller, rect), model (model_)
	{
		setIdleRate (33);
	}

	bool PLUGIN_API open (void* parent, const PlatformType& platformType) override
	{
		if (frame)
			return false;
		const CRect all (0, 0, kWindowWidth, kHeight);
		auto* f = new CFrame (all, this);
		frame = f;
		f->open (parent, platformType);
		f->addView (new Chart (all));

		const ScopeData& scope = model->scopeData ();
		const int groups[3][3] = {{kBodyLevel, kBodyTone, kBodyDrive}, {kSubLevel, kSubTone, kSubDrive}, {kDeepLevel, kDeepTone, kDeepDrive}};
		const char* names[3] = {"Level", "Tone", "Drive"};
		const std::atomic<float>* waves[3] = {scope.body, scope.sub, scope.deep};
		for (int band = 0; band < 3; ++band)
		{
			const CCoord top = kSurfaceHeight + band * kBandHeight;
			for (int i = 0; i < 3; ++i)
				addKnob (f, CPoint (132 + i * kKnobWidth, top + 8), groups[band][i], names[i], layerViews);
			auto* wave = new Wave (CRect (346, top + 10, kWindowWidth - 18, top + kBandHeight - 10), waveWindow, waves[band]);
			f->addView (wave);
			waveViews.push_back (wave);
			layerViews.push_back (wave);
			const int switches[3] = {kBodyOn, kSubOn, kDeepOn};
			auto* layerSwitch = new StageSwitch (CRect (18, top + 66, 74, top + 90), this, switches[band]);
			f->addView (layerSwitch);
			controls.push_back (layerSwitch);
			layerViews.push_back (layerSwitch);
		}

		addKnob (f, CPoint (18, kBedrockTop - 2), kCrossover, "Crossover", layerViews);
		auto* slope = new Segments (CRect (98, kBedrockTop + 38, 268, kBedrockTop + 68), this, kSlope, {"12", "24", "48"});
		f->addView (slope);
		controls.push_back (slope);
		layerViews.push_back (slope);
		auto* solo = new StageSwitch (CRect (222, kBedrockTop + 12, 268, kBedrockTop + 34), this, kSoloBass, "Solo", "Solo");
		f->addView (solo);
		controls.push_back (solo);
		layerViews.push_back (solo);
		addKnob (f, CPoint (kWindowWidth - 18 - 7 * kKnobWidth, kBedrockTop - 2), kInputGain, "Input", layerViews);
		addKnob (f, CPoint (kWindowWidth - 18 - 6 * kKnobWidth, kBedrockTop - 2), kDryBass, "Dry bass", layerViews);
		addKnob (f, CPoint (kWindowWidth - 18 - 5 * kKnobWidth, kBedrockTop - 2), kHighBand, "High", layerViews);
		addKnob (f, CPoint (kWindowWidth - 18 - 4 * kKnobWidth, kBedrockTop - 2), kDry, "Dry", layerViews);
		addKnob (f, CPoint (kWindowWidth - 18 - 3 * kKnobWidth, kBedrockTop - 2), kWet, "Wet", layerViews);
		addKnob (f, CPoint (kWindowWidth - 18 - 2 * kKnobWidth, kBedrockTop - 2), kWetHp, "Wet cut", layerViews);
		addKnob (f, CPoint (kWindowWidth - 18 - kKnobWidth, kBedrockTop - 2), kOutput, "Layer Out", layerViews);

		buildMasterPage (f);

		buildEqPage (f);
		buildMultiPage (f);

		auto* tabs = new Segments (CRect (150, 34, 410, 66), this, kPageTag, {"Layers", "EQ", "Multi", "Master"}, true);
		tabs->setValueNormalized (0.f);
		f->addView (tabs);

		presetMenu = new PresetMenu (CRect (510, 34, 660, 66), this, kPreset);
		presetMenu->setFont (font (12));
		presetMenu->setHoriAlign (kLeftText);
		presetMenu->setTextInset (CPoint (12, 0));
		presetMenu->setFontColor (kWhite);
		presetMenu->setBackColor (kBlack);
		presetMenu->setFrameColor (kLine);
		for (int i = 0; i < model->presetCount (); ++i)
			presetMenu->addEntry (model->presetName (i));
		presetMenu->setMin (0.f);
		presetMenu->setMax (static_cast<float> (model->presetCount () - 1));
		f->addView (presetMenu);

		note = new NoteReadout (CRect (418, 34, 498, 66), scope);
		f->addView (note);

		auto* bypass = new BypassButton (CRect (672, 34, 762, 66), this, kBypass);
		f->addView (bypass);
		controls.push_back (bypass);

		syncControls ();
		dimUnusedControls ();
		showPage (0);
		return true;
	}

	void PLUGIN_API close () override
	{
		controls.clear ();
		waveViews.clear ();
		layerViews.clear ();
		masterViews.clear ();
		eqViews.clear ();
		multiViews.clear ();
		for (auto& views : eqBandViews)
			views.clear ();
		eqGraph = nullptr;
		eqBandTabs = nullptr;
		bandMeters = nullptr;
		history = nullptr;
		meterPanel = nullptr;
		sidechainStatus = nullptr;
		presetMenu = nullptr;
		note = nullptr;
		if (frame)
		{
			auto* f = frame;
			frame = nullptr;
			f->forget ();
		}
	}

	void valueChanged (CControl* control) override
	{
		if (control->getTag () == kPageTag)
		{
			showPage (static_cast<int> (control->getValueNormalized () * 3.f + 0.5f));
			return;
		}
		if (control->getTag () == kEqBandTag)
		{
			selectBand (static_cast<int> (control->getValueNormalized () * 4.f + 0.5f));
			return;
		}
		const auto tag = static_cast<Steinberg::Vst::ParamID> (control->getTag ());
		const double norm = control->getValueNormalized ();
		if (auto* c = getController ())
		{
			c->setParamNormalized (tag, norm);
			c->performEdit (tag, norm);
		}
	}

	CMessageResult notify (CBaseObject* sender, const char* message) override
	{
		if (message == CVSTGUITimer::kMsgTimer && frame)
		{
			syncControls ();
			dimUnusedControls ();
			if (note)
				note->refresh ();
			if (history)
				history->sample ();
			if (page == 3)
			{
				if (meterPanel)
					meterPanel->refresh ();
				if (sidechainStatus)
					sidechainStatus->refresh (model->currentValue (kDuckDepth) > 0.0, model->currentValue (kDuckSource) > 0.1);
			}
			else if (page == 1)
			{
				if (eqGraph)
					eqGraph->refresh ();
			}
			else if (page == 2)
			{
				if (bandMeters)
					bandMeters->invalid ();
				if (history)
					history->invalid ();
			}
			else
			{
				waveWindow.update (model->scopeData ());
				for (auto* wave : waveViews)
					wave->invalid ();
			}
		}
		return VSTGUIEditor::notify (sender, message);
	}

private:
	void addKnob (CFrame* f, const CPoint& where, int param, const char* label, std::vector<CView*>& page)
	{
		auto* knob = new Knob (where, this, param, label, static_cast<float> (model->defaultValue (param)), readout (param));
		f->addView (knob);
		controls.push_back (knob);
		page.push_back (knob);
	}

	void buildMasterPage (CFrame* f)
	{
		auto* chart = new MasterChart (CRect (0, kSurfaceHeight, kWindowWidth, kHeight));
		f->addView (chart);
		masterViews.push_back (chart);

		const auto row = [&] (int band, std::initializer_list<std::pair<int, const char*>> knobs) {
			const CCoord top = kSurfaceHeight + band * kMasterBandHeight + 4;
			int i = 0;
			for (const auto& k : knobs)
				addKnob (f, CPoint (132 + i++ * kKnobWidth, top), k.first, k.second, masterViews);
		};
		row (0, {{kDuckDepth, "Depth"}, {kDuckBand, "Band"}, {kDuckThreshold, "Threshold"}, {kDuckRelease, "Release"}});
		row (1, {{kWidth, "Width"}, {kMonoBelow, "Mono below"}, {kSquash, "Squash"}, {kLowShelf, "Low"}, {kHighShelf, "High"}, {kPunch, "Punch"}});
		row (2, {{kHeat, "Heat"}, {kClipDrive, "Drive"}, {kClipSoft, "Softness"}, {kCleanBelow, "Clean below"}});
		row (3, {{kLimitPush, "Push"}, {kCeiling, "Ceiling"}, {kLimitRelease, "Release"}});

		const auto stageSwitch = [&] (int band, int param) {
			const CCoord top = kSurfaceHeight + band * kMasterBandHeight;
			auto* s = new StageSwitch (CRect (18, top + 66, 74, top + 90), this, param);
			f->addView (s);
			controls.push_back (s);
			masterViews.push_back (s);
		};
		stageSwitch (2, kClipOn);
		stageSwitch (3, kLimitOn);

		sidechainStatus = new SidechainStatus (CRect (402, kSurfaceHeight + 46, 530, kSurfaceHeight + 86), model->meterData ());
		f->addView (sidechainStatus);
		masterViews.push_back (sidechainStatus);

		const CCoord clipTop = kSurfaceHeight + 2 * kMasterBandHeight, limitTop = kSurfaceHeight + 3 * kMasterBandHeight;
		addKnob (f, CPoint (132 + 4 * kKnobWidth, clipTop + 4), kClipCeiling, "Ceiling", masterViews);
		auto* style = new Segments (CRect (336, limitTop + 6, 532, limitTop + 32), this, kLimitStyle, {"Man", "Clean", "Hard", "Mod", "Safe"});
		f->addView (style);
		controls.push_back (style);
		masterViews.push_back (style);
		const auto limitSwitch = [&] (const CRect& r, int param, const char* name) {
			auto* s = new StageSwitch (r, this, param, name, name);
			f->addView (s);
			controls.push_back (s);
			masterViews.push_back (s);
		};
		limitSwitch (CRect (336, limitTop + 38, 396, limitTop + 62), kLimitUnity, "Unity");
		limitSwitch (CRect (400, limitTop + 38, 468, limitTop + 62), kLimitAudition, "Audition");
		limitSwitch (CRect (472, limitTop + 38, 532, limitTop + 62), kDcFilter, "DC");
		auto* dither = new Segments (CRect (336, limitTop + 68, 532, limitTop + 94), this, kDither, {"No dither", "16", "24"});
		f->addView (dither);
		controls.push_back (dither);
		masterViews.push_back (dither);

		auto* source = new Segments (CRect (402, kSurfaceHeight + 12, 530, kSurfaceHeight + 40), this, kDuckSource, {"Kick", "1/2", "1/4", "1/8"});
		f->addView (source);
		controls.push_back (source);
		masterViews.push_back (source);

		meterPanel = new MeterPanel (CRect (538, kSurfaceHeight + 8, kWindowWidth - 8, kHeight - 8), model->meterData ());
		f->addView (meterPanel);
		masterViews.push_back (meterPanel);
	}

	void buildEqPage (CFrame* f)
	{
		auto* floor = new MasterChart (CRect (0, kSurfaceHeight, kWindowWidth, kHeight), true);
		f->addView (floor);
		eqViews.push_back (floor);

		eqGraph = new EqGraph (CRect (0, kSurfaceHeight, kWindowWidth, kEqGraphBottom), model, getController (), [this] (int band) {
			selectBand (band);
		});
		f->addView (eqGraph);
		eqViews.push_back (eqGraph);

		eqBandTabs = new Segments (CRect (18, kEqGraphBottom + 12, 404, kEqGraphBottom + 40), this, kEqBandTag, {"1", "2", "3", "4", "5"});
		f->addView (eqBandTabs);
		eqViews.push_back (eqBandTabs);

		for (int band = 0; band < 5; ++band)
		{
			const int first = kEqFirst + band * 4;
			auto* type = new Segments (CRect (18, kEqGraphBottom + 54, 404, kEqGraphBottom + 84), this, first, {"Bell", "Lo sh", "Hi sh", "Lo cut", "Hi cut", "Notch", "Pass", "Tilt", "All"});
			f->addView (type);
			controls.push_back (type);
			eqBandViews[band].push_back (type);
			auto* place = new Segments (CRect (18, kEqGraphBottom + 100, 262, kEqGraphBottom + 130), this, kEqPlaceFirst + band, {"L", "R", "Stereo", "M", "S"});
			f->addView (place);
			controls.push_back (place);
			eqBandViews[band].push_back (place);
			auto* cutSlope = new Segments (CRect (276, kEqGraphBottom + 100, 404, kEqGraphBottom + 130), this, kEqSlopeFirst + band, {"12", "24", "48"});
			f->addView (cutSlope);
			controls.push_back (cutSlope);
			eqBandViews[band].push_back (cutSlope);
			addKnob (f, CPoint (420, kEqGraphBottom + 30), first + 1, "Frequency", eqBandViews[band]);
			addKnob (f, CPoint (420 + kKnobWidth + 20, kEqGraphBottom + 30), first + 2, "Gain", eqBandViews[band]);
			addKnob (f, CPoint (420 + 2 * (kKnobWidth + 20), kEqGraphBottom + 30), first + 3, "Q", eqBandViews[band]);
		}
	}

	void buildMultiPage (CFrame* f)
	{
		auto* chart = new MultiChart (CRect (0, kSurfaceHeight, kWindowWidth, kHeight));
		f->addView (chart);
		multiViews.push_back (chart);
		const auto row = [&] (int band, std::initializer_list<std::pair<int, const char*>> knobs) {
			const CCoord top = kSurfaceHeight + band * kMasterBandHeight + 4;
			int i = 0;
			for (const auto& k : knobs)
				addKnob (f, CPoint (132 + i++ * kKnobWidth, top), k.first, k.second, multiViews);
		};
		row (0, {{kSquash, "Down"}, {kSquashUp, "Up"}, {kSquashTime, "Time"}, {kThreshLow, "Low at"}, {kThreshMid, "Mid at"}, {kThreshHigh, "High at"}});
		row (1, {{kSplitLow, "Low split"}, {kSplitHigh, "High split"}, {kBandLow, "Low"}, {kBandMid, "Mid"}, {kBandHigh, "High"}});
		const CCoord actionTop = kSurfaceHeight + 2 * kMasterBandHeight + 8;
		bandMeters = new BandMeters (CRect (140, actionTop, 372, kHeight - 8), model->meterData ());
		f->addView (bandMeters);
		multiViews.push_back (bandMeters);
		const CCoord rowHeight = (kHeight - 8 - actionTop) / 3;
		const int modes[3] = {kModeLow, kModeMid, kModeHigh};
		for (int b = 0; b < 3; ++b)
		{
			const CCoord top = actionTop + b * rowHeight + rowHeight / 2 - 14;
			auto* mode = new Segments (CRect (378, top, 530, top + 28), this, modes[b], {"Both", "Up", "Down", "Off"});
			f->addView (mode);
			controls.push_back (mode);
			multiViews.push_back (mode);
		}
		auto* listen = new Segments (CRect (18, kSurfaceHeight + 2 * kMasterBandHeight + 66, 132, kSurfaceHeight + 2 * kMasterBandHeight + 94), this, kListen, {"All", "L", "M", "H"});
		f->addView (listen);
		controls.push_back (listen);
		multiViews.push_back (listen);
		history = new LoudnessHistory (CRect (538, kSurfaceHeight + 8, kWindowWidth - 8, kHeight - 8), model->meterData ());
		f->addView (history);
		multiViews.push_back (history);
	}

	void selectBand (int band)
	{
		eqBand = std::clamp (band, 0, 4);
		if (eqBandTabs)
		{
			eqBandTabs->setValueNormalized (static_cast<float> (eqBand) / 4.f);
			eqBandTabs->invalid ();
		}
		if (eqGraph)
			eqGraph->select (eqBand);
		for (int b = 0; b < 5; ++b)
			for (auto* view : eqBandViews[b])
				view->setVisible (page == 1 && b == eqBand);
		if (frame)
			frame->invalid ();
	}

	void showPage (int next)
	{
		page = next;
		for (auto* view : layerViews)
			view->setVisible (page == 0);
		for (auto* view : eqViews)
			view->setVisible (page == 1);
		for (auto* view : multiViews)
			view->setVisible (page == 2);
		for (auto* view : masterViews)
			view->setVisible (page == 3);
		selectBand (eqBand);
	}

	Formatter readout (int param)
	{
		const bool hz = param == kCrossover || param == kDuckBand || param == kMonoBelow || param == kCleanBelow || param == kWetHp || param == kSplitLow || param == kSplitHigh || (isEqParam (param) && (param - kEqFirst) % 4 == 1);
		const bool isQ = isEqParam (param) && (param - kEqFirst) % 4 == 3;
		const bool db = param == kThreshLow || param == kThreshMid || param == kThreshHigh || param == kClipCeiling || param == kBandLow || param == kBandMid || param == kBandHigh || (isEqParam (param) && (param - kEqFirst) % 4 == 2) || param == kInputGain || param == kOutput || param == kDuckThreshold || param == kLowShelf || param == kHighShelf || param == kClipDrive || param == kLimitPush || param == kCeiling;
		const bool ms = param == kDuckRelease || param == kLimitRelease;
		const char* units = hz ? " Hz" : db ? " dB" : ms ? " ms" : isQ ? "" : "%";
		return [this, param, units] (float norm) {
			Steinberg::Vst::String128 wide {};
			std::string out;
			if (auto* c = getController ())
				if (c->getParamStringByValue (static_cast<Steinberg::Vst::ParamID> (param), norm, wide) == Steinberg::kResultOk)
					for (int i = 0; i < 128 && wide[i]; ++i)
						out.push_back (static_cast<char> (wide[i]));
			return out == "Off" ? out : out + units;
		};
	}

	// True when a control cannot change the sound right now, because of another setting or the
	// signal. These stay usable, so they can be set up in advance.
	bool isUnused (int param)
	{
		if (param >= kEqSlopeFirst)
		{
			const int type = eqTypeFromNorm (model->currentValue (kEqFirst + (param - kEqSlopeFirst) * 4));
			return type != kEqLowCut && type != kEqHighCut;
		}
		if (isEqParam (param))
		{
			const int band = (param - kEqFirst) / 4, field = (param - kEqFirst) % 4;
			const int type = eqTypeFromNorm (model->currentValue (kEqFirst + band * 4));
			const bool gainless = eqIsGainless (type);
			const bool flat = !gainless && std::fabs (eqGainFromNorm (model->currentValue (kEqFirst + band * 4 + 2))) < 0.01;
			if (field == 2)
				return gainless;
			if (field == 3)
				return type == kEqLowShelf || type == kEqHighShelf || type == kEqTilt || flat;
			if (field == 1)
				return flat;
			return false;
		}
		const auto value = [this] (int p) { return model->currentValue (p); };
		const bool bodyOn = value (kBodyOn) >= 0.5 && value (kBodyLevel) > 0.0;
		const bool subOn = value (kSubOn) >= 0.5 && value (kSubLevel) > 0.0;
		const bool deepOn = value (kDeepOn) >= 0.5 && value (kDeepLevel) > 0.0;
		const bool anyLayer = bodyOn || subOn || deepOn;
		const bool clipOn = value (kClipOn) >= 0.5;
		switch (param)
		{
			case kCrossover:
			case kSlope:
			case kWet:
			case kWetHp: return !anyLayer;
			case kBodyLevel: return value (kBodyOn) < 0.5;
			case kSubLevel: return value (kSubOn) < 0.5;
			case kDeepLevel: return value (kDeepOn) < 0.5;
			case kSubOn: return value (kSubLevel) <= 0.0;
			case kDeepOn: return value (kDeepLevel) <= 0.0;
			case kBodyTone:
			case kBodyDrive: return !bodyOn;
			case kSubTone:
			case kSubDrive: return !subOn;
			case kDeepTone:
			case kDeepDrive: return !deepOn;
			case kDryBass: return value (kDry) <= 0.0;
			case kHighBand: return value (kDry) <= 0.0 || value (kSoloBass) >= 0.5;
			case kDuckBand:
			case kDuckRelease: return value (kDuckDepth) <= 0.0;
			case kDuckThreshold: return value (kDuckDepth) <= 0.0 || value (kDuckSource) > 0.1;
			case kDuckSource: return value (kDuckDepth) <= 0.0;
			case kThreshLow:
			case kThreshMid:
			case kThreshHigh:
			case kModeLow:
			case kModeMid:
			case kModeHigh: return value (kSquash) <= 0.0 && value (kSquashUp) <= 0.0;
			case kClipCeiling: return !clipOn;
			case kLimitStyle:
			case kLimitUnity:
			case kLimitAudition: return value (kLimitOn) < 0.5;
			case kSquashTime:
			case kSplitLow:
			case kSplitHigh: return value (kSquash) <= 0.0 && value (kSquashUp) <= 0.0 && value (kBandLow) == 0.5 && value (kBandMid) == 0.5 && value (kBandHigh) == 0.5;
			// Mono material has no side signal for these to act on.
			case kWidth:
			case kMonoBelow: return model->meterData ().correlation.load (std::memory_order_relaxed) >= 0.999f;
			case kHeat:
			case kClipDrive:
			case kClipSoft:
			case kCleanBelow: return !clipOn;
			case kLimitPush:
			case kCeiling: return value (kLimitOn) < 0.5;
			case kLimitRelease: return value (kLimitOn) < 0.5 || value (kLimitStyle) > 0.1;
			default: return false;
		}
	}

	void dimUnusedControls ()
	{
		for (auto* control : controls)
		{
			const float alpha = isUnused (control->getTag ()) ? 0.45f : 1.f;
			if (control->getAlphaValue () != alpha)
			{
				control->setAlphaValue (alpha);
				control->invalid ();
			}
		}
	}

	// Host automation and preset changes reach the window here. A knob being dragged is left alone.
	void syncControls ()
	{
		for (auto* control : controls)
		{
			if (auto* knob = dynamic_cast<Knob*> (control))
				if (knob->isDragging ())
					continue;
			const float value = static_cast<float> (model->currentValue (control->getTag ()));
			// A preset applied on the audio thread also has to reach the stored parameter
			// values, which are what hosts read back.
			if (auto* c = getController ())
			{
				const auto tag = static_cast<Steinberg::Vst::ParamID> (control->getTag ());
				if (std::fabs (c->getParamNormalized (tag) - value) > 0.0005)
					c->setParamNormalized (tag, value);
			}
			if (std::fabs (control->getValueNormalized () - value) > 0.0005f)
			{
				control->setValueNormalized (value);
				control->invalid ();
			}
		}
		if (presetMenu)
		{
			const int count = model->presetCount ();
			const int wanted = static_cast<int> (model->currentValue (kPreset) * (count - 1) + 0.5);
			if (presetMenu->getCurrentIndex () != wanted)
			{
				presetMenu->setCurrent (wanted);
				presetMenu->invalid ();
			}
		}
	}

	EditorModel* model;
	std::vector<CControl*> controls;
	std::vector<CView*> waveViews;
	std::vector<CView*> layerViews;
	std::vector<CView*> masterViews;
	MeterPanel* meterPanel = nullptr;
	SidechainStatus* sidechainStatus = nullptr;
	std::vector<CView*> eqViews;
	std::vector<CView*> multiViews;
	std::vector<CView*> eqBandViews[5];
	EqGraph* eqGraph = nullptr;
	Segments* eqBandTabs = nullptr;
	BandMeters* bandMeters = nullptr;
	LoudnessHistory* history = nullptr;
	int page = 0;
	int eqBand = 0;
	WaveWindow waveWindow;
	COptionMenu* presetMenu = nullptr;
	NoteReadout* note = nullptr;
};

} // namespace

Steinberg::IPlugView* createEditor (Steinberg::Vst::EditController* controller, EditorModel* model)
{
	Steinberg::ViewRect rect (0, 0, static_cast<Steinberg::int32> (kWindowWidth), static_cast<Steinberg::int32> (kHeight));
	return new Editor (controller, model, &rect);
}

} // namespace loopsmaster
