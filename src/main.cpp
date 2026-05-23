//
// main.cpp
// 极简入口：构造并运行 Application；顶层捕获异常打印后退出。
//

#include "app/Application.h"
#include "utils/Log.h"

#include <exception>
#include <cstdlib>

int main() {
    try {
        routes_label::app::Application app;
        app.run();
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal: " << e.what());
        return EXIT_FAILURE;
    } catch (...) {
        LOG_ERROR("Fatal: unknown exception");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
