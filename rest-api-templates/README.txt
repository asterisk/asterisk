This directory contains templates and template processing code for generating
HTTP bindings for the RESTful API's.

The RESTful API's are declared using [Swagger][swagger]. While Swagger provides
a [code generating toolkit][swagger-codegen], it requires Java to run, which
would be an unusual dependency to require for Asterisk developers.

This code generator is similar, but written in Python. Templates are processed
by using [pystache][pystache], which is a fairly simply Python implementation of
[mustache][mustache].

 [swagger]: https://github.com/wordnik/swagger-core/wiki
 [swagger-codegen]: https://github.com/wordnik/swagger-codegen
 [pystache]: https://github.com/defunkt/pystache
 [mustache]: http://mustache.github.io/
