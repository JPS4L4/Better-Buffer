#pragma once

#ifdef BETTER_BUFFER_ENABLE_QT
class BetterBufferStatusDock {
public:
	static void Init();
	static void Shutdown();
	static void SetMessage(const char *message);
};
#else
class BetterBufferStatusDock {
public:
	static void Init() {}
	static void Shutdown() {}
	static void SetMessage(const char *) {}
};
#endif
