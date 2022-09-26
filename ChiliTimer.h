#pragma once
#include <chrono>

class ChiliTimer
{
public:
	ChiliTimer() noexcept
	{
		last = std::chrono::steady_clock::now();
	}
	float Mark() noexcept 
	{
		const auto old = last;
		last = std::chrono::steady_clock::now();
		const std::chrono::duration<float> frameTime = last - old;
		return frameTime.count();
	}
	float Peek() const noexcept
	{
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - last).count();
	}
private:
	std::chrono::steady_clock::time_point last;
};
