#pragma once
#include "ReflectionValue.h"
#include <iosfwd>

namespace NexusEngine {
/** \brief Level拡張Componentの値ツリーを型情報付きの可読テキストへ保存する */
void WriteReflectionTextArchive(std::ostream& output, const ReflectionValue& value);
/** \brief サイズ・深さ・重複キーを検証し、値ツリーを復元する。破損時は例外を返す */
ReflectionValue ReadReflectionTextArchive(std::istream& input);
}
