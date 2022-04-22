#pragma once

#include <rack.hpp>

using namespace rack;

extern Plugin *pluginInstance;

std::shared_ptr<rack::Font> getDefaultFont(int which);

inline float customlog(float base, float x)
{
	return std::log(x)/std::log(base);
}

// taken from https://rigtorp.se/spinlock/

struct spinlock {
  std::atomic<bool> lock_ = {0};

  void lock() noexcept {
    for (;;) {
      // Optimistically assume the lock is free on the first try
      if (!lock_.exchange(true, std::memory_order_acquire)) {
        return;
      }
      // Wait for lock to be released without generating cache misses
      while (lock_.load(std::memory_order_relaxed)) {
        // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
        // hyper-threads
        __builtin_ia32_pause();
      }
    }
  }

  bool try_lock() noexcept {
    // First do a relaxed load to check if lock is free in order to prevent
    // unnecessary cache misses if someone does while(!try_lock())
    return !lock_.load(std::memory_order_relaxed) &&
           !lock_.exchange(true, std::memory_order_acquire);
  }

  void unlock() noexcept {
    lock_.store(false, std::memory_order_release);
  }
};


class OnePoleFilter
{
public:
    OnePoleFilter() {}
    void setAmount(float x)
    {
        a = x;
        b = 1.0f-a;
    }
    inline __attribute__((always_inline)) float process(float x)
    {
        float temp = (x * b) + (z * a);
        z = temp;
        return temp;
    }
private:
    float z = 0.0f;
    float a = 0.99f;
    float b = 1.0f-a;

};

inline std::pair<int, int> parseFractional(std::string& str)
{
	int pos = str.find('/');
	auto first = str.substr(0, pos);
	auto second = str.substr(pos + 1);
	return { std::stoi(first),std::stoi(second) };
}

template<typename T>
inline double grid_value(const T& ge)
{
    return ge;
}


#define VAL_QUAN_NORILO

template<typename T,typename Grid>
inline double quantize_to_grid(T x, const Grid& g, double amount=1.0)
{
    auto t1=std::lower_bound(std::begin(g),std::end(g),x);
    if (t1!=std::end(g))
    {
        /*
        auto t0=t1-1;
        if (t0<std::begin(g))
            t0=std::begin(g);
        */
        auto t0=std::begin(g);
        if (t1>std::begin(g))
            t0=t1-1;
#ifndef VAL_QUAN_NORILO
        const T half_diff=(*t1-*t0)/2;
        const T mid_point=*t0+half_diff;
        if (x<mid_point)
        {
            const T diff=*t0-x;
            return x+diff*amount;
        } else
        {
            const T diff=*t1-x;
            return x+diff*amount;
        }
#else
        const double gridvalue = fabs(grid_value(*t0) - grid_value(x)) < fabs(grid_value(*t1) - grid_value(x)) ? grid_value(*t0) : grid_value(*t1);
        return x + amount * (gridvalue - x);
#endif
    }
    auto last = std::end(g)-1;
    const double diff=grid_value(*(last))-grid_value(x);
    return x+diff*amount;
}

template <typename TLightBase = RedLight>
struct LEDLightSliderFixed : LEDLightSlider<TLightBase> {
	LEDLightSliderFixed() {
		this->setHandleSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LEDSliderHandle.svg")));
	}
};



inline float soft_clip(float x)
{
    if (x<-1.0f)
        return -2.0f/3.0f;
    if (x>1.0f)
        return 2.0f/3.0f;
    return x-(std::pow(x,3.0f)/3.0f);
}

// note that limits can't be the same and the loop may run for long!
template<typename T>
inline T wrap_value(const T& minval, const T& val, const T& maxval)
{
	T temp = val;
	while (temp<minval || temp>maxval)
	{
		if (temp < minval)
			temp = maxval - (minval - temp);
		if (temp > maxval)
			temp = minval - (maxval - temp);
	}
	return temp;
}

// safe version that handles the case where limits are the same and the loop has an iteration limit
// if iteration limit is reached, returns value between the limits
template<typename T>
inline T wrap_value_safe(const T& minval, const T& val, const T& maxval, int iterlimit=100)
{
	if (minval==maxval)
		return minval;
	int sanity = 0;
	T temp = val;
	while (temp<minval || temp>maxval)
	{
		if (temp < minval)
			temp = maxval - (minval - temp);
		if (temp > maxval)
			temp = minval - (maxval - temp);
		++sanity;
		if (sanity == iterlimit)
		{
			return minval+(maxval-minval)/2.0f;
		}
	}
	return temp;
}

// note that limits can't be the same and the loop may run for long!
template<typename T>
inline T reflect_value(const T& minval, const T& val, const T& maxval)
{
	T temp = val;
	while (temp<minval || temp>maxval)
	{
		if (temp < minval)
			temp = minval + (minval - temp);
		if (temp > maxval)
			temp = maxval + (maxval - temp);
	}
	return temp;
}

// safe version that handles the case where limits are the same and the loop has an iteration limit
// if iteration limit is reached, returns value between the limits
template<typename T>
inline T reflect_value_safe(const T& minval, const T& val, const T& maxval, int iterlimit=100)
{
	if (minval==maxval)
		return minval;
	int sanity = 0;
	T temp = val;
	while (temp<minval || temp>maxval)
	{
		if (temp < minval)
			temp = minval + (minval - temp);
		if (temp > maxval)
			temp = maxval + (maxval - temp);
		++sanity;
		if (sanity==iterlimit)
		{
			return minval+(maxval-minval)/2.0;
		}
	}
	return temp;
}

struct LambdaItem : rack::MenuItem
{
	std::function<void(void)> ActionFunc;
	void onAction(const event::Action &e) override
	{
		if (ActionFunc)
			ActionFunc();
	}
};

template <class Func>
inline rack::MenuItem * createMenuItem(Func f, std::string text, std::string rightText = "") {
	LambdaItem* o = new LambdaItem;
	o->text = text;
	o->rightText = rightText;
	o->ActionFunc = f;
	return o;
}
