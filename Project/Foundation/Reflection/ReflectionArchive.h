#pragma once
#include "ReflectionValue.h"
#include <iosfwd>

namespace NexusEngine {
// 回復用ファイルも C++ のオブジェクト表現に依存せず、種類付きの値を記録する。
void WriteReflectionArchive(std::ostream& output, const ReflectionValue& value);
ReflectionValue ReadReflectionArchive(std::istream& input);
}
