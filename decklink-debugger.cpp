#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>

#include <atomic>
#include <vector>
#include <string>
#include <iostream>

#include "util.h"
#include "tostring.h"
#include "DeckLinkAPI.h"
#include "DeviceProber.h"
#include "HttpServer.h"
#include "TablePrinter.h"

#include "scope_guard.hpp"
#include "log.h"

static std::atomic<bool> g_do_exit{false};

std::vector<IDeckLink*> collectDeckLinkDevices(void);
void freeDeckLinkDevices(std::vector<IDeckLink*> deckLinkDevices);

std::vector<DeviceProber*> createDeviceProbers(std::vector<IDeckLink*> deckLinkDevices);
void freeDeviceProbers(std::vector<DeviceProber*> deviceProbers);

void printStatusList(std::vector<DeviceProber*> deviceProbers, unsigned int iteration);
char* getDeviceName(IDeckLink* deckLink);

static void sigfunc(int signum);

void _main() {
	LOG(DEBUG) << "collecting DeckLink Devices";
	std::vector<IDeckLink*> deckLinkDevices = collectDeckLinkDevices();
	auto deckLinkDevicesGuard = sg::make_scope_guard([deckLinkDevices]{
		LOG(DEBUG) << "freeing DeckLink Devices";
		freeDeckLinkDevices(deckLinkDevices);
	});

	if(deckLinkDevices.size() == 0)
	{
		throw "No DeckLink devices found";
	}

	LOG(DEBUG) << "creating Device-Probers";
	std::vector<DeviceProber*> deviceProbers = createDeviceProbers(deckLinkDevices);
	auto deviceProbersGuard = sg::make_scope_guard([deviceProbers]{
		LOG(DEBUG) << "freeing Device-Probers";
		freeDeviceProbers(deviceProbers);
	});


	LOG(DEBUG) << "creating HttpServer";
	HttpServer* httpServer = new HttpServer(deviceProbers);
	auto httpServerGuard = sg::make_scope_guard([httpServer]{
		LOG(DEBUG) << "freeing HttpServer";
		assert(httpServer->Release() == 0);
	});

	LOG(DEBUG2) << "registering Signal-Handler";
	signal(SIGINT, sigfunc);
	signal(SIGTERM, sigfunc);
	signal(SIGHUP, sigfunc);

	LOG(DEBUG2) << "entering Display-Loop";
	unsigned int iteration = 0;
	while(!g_do_exit.load(std::memory_order_acquire))
	{
		printStatusList(deviceProbers, iteration++);

		for(DeviceProber* deviceProber: deviceProbers) {
			if(!deviceProber->GetSignalDetected()) {
				deviceProber->SelectNextConnection();
			}
		}
		sleep(1);
	}

	std::cout << "Bye." << std::endl;
}

int main (UNUSED int argc, UNUSED char** argv)
{
	try {
		_main();
	}
	catch(const char* e) {
		std::cerr << "exception cought: " << e << std::endl;
		return 1;
	}
	catch(...) {
		std::cerr << "unknown exception cought" << std::endl;
		return 1;
	}

	return 0;
}

static void sigfunc(int signum)
{
	LOG(INFO) << "cought signal "<< signum;
	if (signum == SIGINT || signum == SIGTERM)
	{
		LOG(DEBUG) << "g_do_exit = true";
		g_do_exit = true;
	}
}

std::vector<IDeckLink*> collectDeckLinkDevices(void)
{
	std::vector<IDeckLink*> deckLinkDevices;
	IDeckLinkIterator*    deckLinkIterator;

	deckLinkIterator = CreateDeckLinkIteratorInstance();
	if (deckLinkIterator == NULL)
	{
		throw "A DeckLink iterator could not be created. "
			"The DeckLink drivers may not be installed.";
	}

	IDeckLink* deckLink = NULL;
	while (deckLinkIterator->Next(&deckLink) == S_OK)
	{
		deckLinkDevices.push_back(deckLink);
	}
	LOG(DEBUG) << "found " << deckLinkDevices.size() << " devices";

	assert(deckLinkIterator->Release() == 0);
	return deckLinkDevices;
}

void freeDeckLinkDevices(std::vector<IDeckLink*> deckLinkDevices)
{
	unsigned int i = 0;
	for(IDeckLink* deckLink: deckLinkDevices)
	{
		i++;
		LOG(DEBUG1) << "freeing device " << i;
		assert(deckLink->Release() == 0);
	}
}

std::vector<DeviceProber*> createDeviceProbers(std::vector<IDeckLink*> deckLinkDevices)
{
	std::vector<DeviceProber*> deviceProbers;

	unsigned int i = 0;
	for(IDeckLink* deckLink: deckLinkDevices)
	{
		i++;
		LOG(DEBUG1) << "creating DeviceProber for Device " << i;
		deviceProbers.push_back(new DeviceProber(deckLink));
	}

	return deviceProbers;
}

void freeDeviceProbers(std::vector<DeviceProber*> deviceProbers)
{
	unsigned int i = 0;
	for(DeviceProber* deviceProber : deviceProbers)
	{
		i++;
		LOG(DEBUG1) << "freeing DeviceProber for Device " << i;
		assert(deviceProber->Release() == 0);
	}
}

void printStatusList(std::vector<DeviceProber*> deviceProbers, unsigned int iteration)
{
	if(iteration > 0)
	{
		int nLines = deviceProbers.size() + 6;
		std::cout << "\033[" << nLines << "A";
	}

	bprinter::TablePrinter table(&std::cout);
	table.AddColumn("#", 15);
	table.AddColumn("Device Name", 31);
	table.AddColumn("Can Input & Detect", 20);
	table.AddColumn("Signal Detected", 17);
	table.AddColumn("Active Connection", 19);
	table.AddColumn("Detected Mode", 16);
	table.AddColumn("Pixel Format", 15);
	table.set_flush_left();
	table.PrintHeader();

	int deviceIndex = 0;
	for(DeviceProber* deviceProber : deviceProbers)
	{
		if(!deviceProber->GetSignalDetected())
		{
			table << bprinter::greyon();
		}

		std::string deviceName = deviceProber->GetDeviceName();
		if(deviceProber->IsSubDevice())
		{
			deviceName = "\\-> " + deviceName;
		}

		table
			<< deviceIndex
			<< deviceName
			<< boolToString(deviceProber->CanAutodetect() && deviceProber->CanInput())
			<< boolToString(deviceProber->GetSignalDetected())
			<< videoConnectionToString(deviceProber->GetActiveConnection())
			<< deviceProber->GetDetectedMode()
			<< pixelFormatToString(deviceProber->GetPixelFormat())
			<< bprinter::greyoff();

		deviceIndex++;
	}
	table.PrintFooter();

	const char iterationSign[4] = { '|', '\\', '-', '/' };
	std::cout << std::endl << "     Scanning... " << iterationSign[iteration % 4] << std::endl;
}
