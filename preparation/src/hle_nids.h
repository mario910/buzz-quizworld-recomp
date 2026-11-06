#pragma once
#include "common.h"

namespace prepare {
int writeHleNids(const QString &sdk, const QString &output, const Cancel &cancel = {});
} // namespace prepare
