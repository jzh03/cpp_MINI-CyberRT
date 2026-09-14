#include <cmw/class_loader/class_loader.h>
#include <cmw/init.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <cmw/class_loader/test/base.h>
#include <cmw/class_loader/class_loader_manager.h>


#ifndef CMW_TEST_PLUGIN_DIR
#error "Build this example through example/Makefile to locate its plugins."
#endif
const char LIBRARY_1[] = CMW_TEST_PLUGIN_DIR "/libplugin1.so";
const char LIBRARY_2[] = CMW_TEST_PLUGIN_DIR "/libplugin2.so";

using hnu::cmw::class_loader::ClassLoader;
using hnu::cmw::class_loader::ClassLoaderManager;
using hnu::cmw::class_loader::utility::IsLibraryLoaded;

void ClassLoaderTest_createClassObj(){
    ClassLoader loader1(LIBRARY_1);
    EXPECT_EQ(LIBRARY_1, loader1.GetLibraryPath());
    auto rect_obj = loader1.CreateClassObj<Base>("Rect");
    EXPECT_NE(nullptr, rect_obj);
    rect_obj->DoSomething();
    EXPECT_EQ(nullptr, loader1.CreateClassObj<Base>("Xeee"));
}

int main()
{
    hnu::cmw::Init("class_loader_test");
    ClassLoaderTest_createClassObj();
    return 0;
}
