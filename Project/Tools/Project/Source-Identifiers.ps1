# 文字列literal/commentは保持し、自己型のidentifier tokenだけを変更する。
function Rename-NexusSourceIdentifier([string]$Text,[string]$Old,[string]$New) {
 $pattern=@'
(?s)//[^\r\n]*|/\*.*?\*/|R"(?<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)\k<delimiter>"|@"(?:""|[^"])*"|""".*?"""|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[A-Za-z_][A-Za-z_0-9]*
'@
 $handler={ param($match)
  if($match.Value -ceq $Old) { return $New }; return $match.Value
 }.GetNewClosure()
 return [regex]::Replace($Text,$pattern,[Text.RegularExpressions.MatchEvaluator]$handler)
}
