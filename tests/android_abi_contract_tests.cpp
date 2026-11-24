// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <string_view>

#include "engine_install.h"
#include "univex_android_editor_abi.h"

TEST(AndroidAbiContractTest, EntryPointSymbolMatchesEngineInstallContract) {
    EXPECT_EQ(std::string_view(UVE_ANDROID_EDITOR_ENTRY_SYMBOL), UVE::Pack::kEditorAndroidEntryPointUVE);
    EXPECT_EQ(UVE_ANDROID_EDITOR_ABI_VERSION, 1U);
    EXPECT_EQ(std::string_view(UVE_ANDROID_EDITOR_ABI_VERSION_SYMBOL), "UveEditorAbiVersionUVE");
}
