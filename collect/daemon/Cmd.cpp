#include <cstdio>
#include <exception>

#include "Collect.h"

int main(int argc, char** argv)
{
	try
	{
		CollectDaemon& ins = CollectDaemon::instance();
		ins.configure(argc, argv);
		return ins.run();
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "Unhandled exception: %s\n", e.what());
		return EXIT_FAILURE;
	}
}

