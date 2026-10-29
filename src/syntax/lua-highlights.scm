; SPDX-License-Identifier: GPL-3.0-or-later
; What each part of a Lua source is, for coloring. Where two patterns capture the same
; text, the earlier one wins; where one capture lies inside another, the inner one wins.

(comment) @comment
(string) @string
(number) @number

[
  (nil)
  (true)
  (false)
  (vararg_expression)
] @constant

[
  "and" "or" "not"
  "local" "global" "function" "return"
  "if" "then" "elseif" "else" "end"
  "for" "in" "do" "while" "repeat" "until" "goto"
] @keyword

(break_statement) @keyword

(function_call name: (identifier) @function)
(function_call name: (dot_index_expression field: (identifier) @function))
(function_call name: (method_index_expression method: (identifier) @function))
(function_declaration name: (identifier) @function)
(function_declaration name: (dot_index_expression field: (identifier) @function))
(function_declaration name: (method_index_expression method: (identifier) @function))

(field name: (identifier) @field)
(dot_index_expression field: (identifier) @field)
(method_index_expression method: (identifier) @field)

[
  "=" "==" "~=" "<" ">" "<=" ">="
  "+" "-" "*" "/" "//" "%" "^" "#" ".."
  "&" "|" "~" "<<" ">>"
] @operation

[
  "(" ")" "[" "]" "{" "}"
  "," ";" "." ":"
] @punctuation
