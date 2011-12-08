" Vim syntax file
" Language:	Asterisk Extension Language
" Maintainer:	tilghman
" Last Change:	2009 Mar 04 
" version 0.1
"
if version < 600
  syntax clear
elseif exists("b:current_syntax")
  finish
endif

syn sync clear
syn sync fromstart

syn keyword     aelTodo            TODO contained
syn keyword     aelKeyword         context includes macro globals if else switch case default catch return switches includes for ignorepat
syn match       aelComment         "//.*" contains=aelTodo
syn match       aelContext         "\s+\zs[[:alpha:]][[:alnum:]\-_]*\ze\s*{"
" Macro declaration
syn match       aelMacro           "[[:alpha:]][[:alnum:]\-_]*(.\{-})\ze\s*{" contains=aelVar,aelFunction,aelExp,aelArgs
" Macro reference
syn match       aelMacro           "&[[:alpha:]][[:alnum:]\-_]*(.\{-});" contains=aelVar,aelFunction,aelExp,aelArgs
" Ranges or other pattern characters
syn match       aelExten           "\zs_\(\[[[:alnum:]#*\-]\+\]\|[[:alnum:]#*\-]\)\+[\.!]\?\ze\s+=>"
syn match       aelExten           "\zs[[:alnum:]#*]\+\ze\s*=>"
syn match       aelApp             "\s\+\zs[[:alpha:]][[:alpha:]_]\+\ze[; ]"
syn match       aelApp             "\s\+\zs[[:alpha:]][[:alpha:]_]\+\ze(.\{-});"
syn match       aelLabel           "[[:alpha:]][[:alnum:]]*\ze:"
syn region      aelVar             matchgroup=aelVarStart start="\${" end="}" contains=aelVar,aelFunction,aelExp
syn match       aelVar             "\zs[[:alpha:]][[:alnum:]_]*\ze=" contains=aelVar,aelFunction,aelExp
" Retrieving the value of a function
syn match       aelFunction        "\${_\{0,2}[[:alpha:]][[:alnum:]_]*(.\{-})}" contains=aelVar,aelFunction,aelExp
" Setting a function
syn match       aelFunction        "(\zs[[:alpha:]][[:alnum:]_]*(.\{-})\ze=" contains=aelVar,aelFunction,aelExp
syn region      aelExp             matchgroup=aelExpStart start="\$\[" end="]" contains=aelVar,aelFunction,aelExp
syn match       aelArgs            "([[:alnum:]_, ]*)" contains=aelArgsElement contained
syn match       aelArgsElement     "[[:alpha:]][[:alnum:]_]*" contained

" Define the default highlighting.
" For version 5.7 and earlier: only when not done already
" For version 5.8 and later: only when an item doesn't have highlighting yet
if version >= 508 || !exists("did_conf_syntax_inits")
  if version < 508
    let did_conf_syntax_inits = 1
    command -nargs=+ HiLink hi link <args>
  else
    command -nargs=+ HiLink hi def link <args>
  endif

  HiLink        aelComment         Comment
  HiLink        aelContext         Preproc
  HiLink        aelMacro           Preproc
  HiLink        aelExten           Type
  HiLink        aelLabel           Type
  HiLink        aelApp             Preproc
  HiLink        aelVar             String
  HiLink        aelVarStart        String
  HiLink        aelArgsElement     String
  HiLink        aelFunction        Function
  HiLink        aelExp             Type
  HiLink        aelExpStart        Type
  HiLink        aelKeyword         Statement
  HiLink        aelError           Error
 delcommand HiLink
endif
let b:current_syntax = "ael" 
" vim: ts=8 sw=2

