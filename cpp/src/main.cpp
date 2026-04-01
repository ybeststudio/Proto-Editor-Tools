#include "ProtoEditorApp.h"
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    ProtoEditorApp app(hInstance);
    return app.run();
}
