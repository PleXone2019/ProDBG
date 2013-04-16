#include <ProDBGAPI.h>
#include <stdlib.h> 
#include <stdio.h> 
#include <string.h> 

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct LLDBPlugin
{
	int someData; ///

} LLDBPlugin;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* createInstance(ServiceFunc* serviceFunc)
{
	LLDBPlugin* plugin = malloc(sizeof(LLDBPlugin));
	memset(plugin, 0, sizeof(LLDBPlugin));
	return plugin;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void destroyInstance(void* userData)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void onBreak(LLDBPlugin* data, void* actionData)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void onStep(LLDBPlugin* data, void* actionData)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void onStepOver(LLDBPlugin* data, void* actionData)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void onSetCodeBreakpoint(LLDBPlugin* data, void* actionData)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void actionCallback(void* userData, PDDebugAction action, void* actionData)
{
	LLDBPlugin* plugin = (LLDBPlugin*)userData;

	switch (action)
	{
		case PD_DEBUG_ACTION_BREAK : onBreak(plugin, actionData); break;
		case PD_DEBUG_ACTION_STEP : onStep(plugin, actionData); break;
		case PD_DEBUG_ACTION_STEP_OVER : onStepOver(plugin, actionData); break;
		case PD_DEBUG_ACTION_SET_CODE_BREAKPOINT : onSetCodeBreakpoint(plugin, actionData); break;
		default:
			break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDDebugPlugin plugin =
{
	createInstance,
	destroyInstance,
	actionCallback,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void InitPlugin(int version, ServiceFunc* serviceFunc, RegisterPlugin* registerPlugin)
{
	registerPlugin(PD_DEBUGGER_TYPE, &plugin);
}

