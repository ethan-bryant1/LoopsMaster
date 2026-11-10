// Small building blocks shared by the bass layers and the master chain.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace loopsmaster {

class Biquad {
public:
	static constexpr double kPi = 3.14159265358979323846;

	void setLowPass (double hz, double q, double sampleRate)
	{
		const double w = 2.0 * kPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
		const double alpha = std::sin (w) / (2.0 * q);
		const double c = std::cos (w);
		const double a0 = 1.0 + alpha;
		setRaw ((1.0 - c) * 0.5 / a0, (1.0 - c) / a0, (1.0 - c) * 0.5 / a0, -2.0 * c / a0, (1.0 - alpha) / a0);
	}
	void setHighPass (double hz, double q, double sampleRate)
	{
		const double w = 2.0 * kPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
		const double alpha = std::sin (w) / (2.0 * q);
		const double c = std::cos (w);
		const double a0 = 1.0 + alpha;
		setRaw ((1.0 + c) * 0.5 / a0, -(1.0 + c) / a0, (1.0 + c) * 0.5 / a0, -2.0 * c / a0, (1.0 - alpha) / a0);
	}
	void setLowShelf (double hz, double gainDb, double sampleRate)
	{
		const double A = std::pow (10.0, gainDb / 40.0);
		const double w = 2.0 * kPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
		const double c = std::cos (w);
		const double alpha = std::sin (w) * 0.70710678;
		const double r = 2.0 * std::sqrt (A) * alpha;
		const double a0 = (A + 1.0) + (A - 1.0) * c + r;
		setRaw (A * ((A + 1.0) - (A - 1.0) * c + r) / a0, 2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0,
		        A * ((A + 1.0) - (A - 1.0) * c - r) / a0, -2.0 * ((A - 1.0) + (A + 1.0) * c) / a0,
		        ((A + 1.0) + (A - 1.0) * c - r) / a0);
	}
	void setHighShelf (double hz, double gainDb, double sampleRate)
	{
		const double A = std::pow (10.0, gainDb / 40.0);
		const double w = 2.0 * kPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
		const double c = std::cos (w);
		const double alpha = std::sin (w) * 0.70710678;
		const double r = 2.0 * std::sqrt (A) * alpha;
		const double a0 = (A + 1.0) - (A - 1.0) * c + r;
		setRaw (A * ((A + 1.0) + (A - 1.0) * c + r) / a0, -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0,
		        A * ((A + 1.0) + (A - 1.0) * c - r) / a0, 2.0 * ((A - 1.0) - (A + 1.0) * c) / a0,
		        ((A + 1.0) - (A - 1.0) * c - r) / a0);
	}
	void setPeak (double hz, double gainDb, double q, double sampleRate)
	{
		const double A = std::pow (10.0, gainDb / 40.0);
		const double w = 2.0 * kPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
		const double alpha = std::sin (w) / (2.0 * q);
		const double c = std::cos (w);
		const double a0 = 1.0 + alpha / A;
		setRaw ((1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha / A) / a0);
	}
	void setNotch (double hz, double q, double sampleRate)
	{
		const double w = 2.0 * kPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
		const double alpha = std::sin (w) / (2.0 * q);
		const double c = std::cos (w);
		const double a0 = 1.0 + alpha;
		setRaw (1.0 / a0, -2.0 * c / a0, 1.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0);
	}
	void setBandPass (double hz, double q, double sampleRate)
	{
		const double w = 2.0 * kPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
		const double alpha = std::sin (w) / (2.0 * q);
		const double c = std::cos (w);
		const double a0 = 1.0 + alpha;
		setRaw (alpha / a0, 0.0, -alpha / a0, -2.0 * c / a0, (1.0 - alpha) / a0);
	}
	void setAllPass (double hz, double q, double sampleRate)
	{
		const double w = 2.0 * kPi * std::min (hz, 0.45 * sampleRate) / sampleRate;
		const double alpha = std::sin (w) / (2.0 * q);
		const double c = std::cos (w);
		const double a0 = 1.0 + alpha;
		setRaw ((1.0 - alpha) / a0, -2.0 * c / a0, 1.0, -2.0 * c / a0, (1.0 - alpha) / a0);
	}
	// A high shelf with half its gain taken back, so the ends move in opposite directions.
	void setTilt (double hz, double gainDb, double sampleRate)
	{
		setHighShelf (hz, gainDb, sampleRate);
		const double trim = std::pow (10.0, -gainDb / 40.0);
		b0 *= trim;
		b1 *= trim;
		b2 *= trim;
	}
	// The filter's level at one frequency, for drawing curves.
	double magnitudeDb (double hz, double sampleRate) const
	{
		const double w = 2.0 * kPi * std::min (hz, 0.49 * sampleRate) / sampleRate;
		const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
		const double nr = b0 + b1 * c1 + b2 * c2, ni = -b1 * s1 - b2 * s2;
		const double dr = 1.0 + a1 * c1 + a2 * c2, di = -a1 * s1 - a2 * s2;
		return 10.0 * std::log10 (std::max (nr * nr + ni * ni, 1e-20) / std::max (dr * dr + di * di, 1e-20));
	}
	void setRaw (double b0_, double b1_, double b2_, double a1_, double a2_)
	{
		b0 = b0_;
		b1 = b1_;
		b2 = b2_;
		a1 = a1_;
		a2 = a2_;
	}
	double process (double x)
	{
		const double y = b0 * x + z1;
		z1 = flush (b1 * x - a1 * y + z2);
		z2 = flush (b2 * x - a2 * y);
		return y;
	}
	void reset () { z1 = z2 = 0.0; }

private:
	static double flush (double v) { return std::fabs (v) < 1e-30 ? 0.0 : v; }
	double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
};

class OnePole {
public:
	void set (double hz, double sampleRate)
	{
		a = 1.0 - std::exp (-2.0 * Biquad::kPi * std::min (hz, 0.45 * sampleRate) / sampleRate);
	}
	double process (double x)
	{
		y += a * (x - y);
		if (std::fabs (y) < 1e-30)
			y = 0.0;
		return y;
	}
	void reset () { y = 0.0; }

private:
	double a = 1.0, y = 0.0;
};

class Smoothed {
public:
	void setRate (double sampleRate) { k = 1.0 - std::exp (-1.0 / (0.005 * sampleRate)); }
	void jump (double v) { value = target = v; }
	void setTarget (double v) { target = v; }
	double next () { return value += k * (target - value); }
	double get () const { return value; }
	bool settledAt (double v) const { return target == v && std::fabs (value - v) < 1e-7; }

private:
	double k = 1.0, value = 0.0, target = 0.0;
};

class DelayLine {
public:
	void prepare (int samples)
	{
		data.assign (static_cast<size_t> (std::max (samples, 0)) + 1, 0.0);
		pos = 0;
	}
	double process (double x)
	{
		data[pos] = x;
		pos = pos + 1 == data.size () ? 0 : pos + 1;
		return data[pos];
	}
	void reset () { std::fill (data.begin (), data.end (), 0.0); }

private:
	std::vector<double> data {0.0};
	size_t pos = 0;
};

// A newest-first history that can be read as one contiguous array.
class History {
public:
	void prepare (int length)
	{
		size = length;
		data.assign (static_cast<size_t> (length) * 2, 0.0);
		pos = 0;
	}
	void push (double x)
	{
		pos = pos == 0 ? size - 1 : pos - 1;
		data[pos] = data[pos + size] = x;
	}
	const double* newest () const { return data.data () + pos; }
	void reset () { std::fill (data.begin (), data.end (), 0.0); }

private:
	std::vector<double> data;
	int size = 0, pos = 0;
};

// A Kaiser-windowed low-pass kernel. cutoff is a fraction of the rate it runs at.
inline std::vector<double> makeKernel (int taps, double cutoff, double beta)
{
	const auto bessel = [] (double x) {
		double sum = 1.0, term = 1.0;
		for (int k = 1; k < 40; ++k)
		{
			term *= (x / (2.0 * k)) * (x / (2.0 * k));
			sum += term;
		}
		return sum;
	};
	std::vector<double> h (static_cast<size_t> (taps));
	const double middle = 0.5 * (taps - 1);
	for (int i = 0; i < taps; ++i)
	{
		const double t = i - middle;
		const double sinc = t == 0.0 ? 2.0 * cutoff : std::sin (2.0 * Biquad::kPi * cutoff * t) / (Biquad::kPi * t);
		const double r = t / middle;
		h[static_cast<size_t> (i)] = sinc * bessel (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel (beta);
	}
	return h;
}

} // namespace loopsmaster
