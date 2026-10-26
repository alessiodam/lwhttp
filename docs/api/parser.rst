Parser
======

An incremental HTTP/1.1 response parser. Feed it bytes as they arrive, in pieces of any size; it never copies the
body and never allocates. The client uses it internally, and it is exported for programs with their own transport.

.. code-block:: c

   static char line[1024];
   lwhttp_parser_t parser;

   lwhttp_parser_init(&parser, line, sizeof(line), &callbacks, &state);
   lwhttp_parser_set_request(&parser, "GET");
   while (more_data) {
     size_t used = lwhttp_parser_execute(&parser, data, len);
     if (lwhttp_parser_error(&parser)) break;
     data += used;
     len -= used;
   }

Types
-----

.. c:autostruct:: lwhttp_parser
   :file: lwhttp.h
   :members:

.. c:autotype:: lwhttp_parser_t
   :file: lwhttp.h

.. c:autostruct:: lwhttp_parser_callbacks_t
   :file: lwhttp.h
   :members:

Message flags
-------------

.. c:automacro:: LWHTTP_MSG_CHUNKED
   :file: lwhttp.h

.. c:automacro:: LWHTTP_MSG_CONTENT_LENGTH
   :file: lwhttp.h

.. c:automacro:: LWHTTP_MSG_KEEP_ALIVE
   :file: lwhttp.h

.. c:automacro:: LWHTTP_MSG_UPGRADE
   :file: lwhttp.h

.. c:automacro:: LWHTTP_MSG_CLOSE_DELIMITED
   :file: lwhttp.h

.. c:automacro:: LWHTTP_MSG_TRANSFER_CODED
   :file: lwhttp.h

.. c:automacro:: LWHTTP_MSG_NO_BODY
   :file: lwhttp.h

.. c:automacro:: LWHTTP_PARSER_SKIP_BODY
   :file: lwhttp.h

Functions
---------

.. c:autofunction:: lwhttp_parser_init
   :file: lwhttp.h

.. c:autofunction:: lwhttp_parser_reset
   :file: lwhttp.h

.. c:autofunction:: lwhttp_parser_set_request
   :file: lwhttp.h

.. c:autofunction:: lwhttp_parser_execute
   :file: lwhttp.h

.. c:autofunction:: lwhttp_parser_finish
   :file: lwhttp.h

.. c:autofunction:: lwhttp_parser_is_complete
   :file: lwhttp.h

.. c:autofunction:: lwhttp_parser_keep_alive
   :file: lwhttp.h

.. c:autofunction:: lwhttp_parser_error
   :file: lwhttp.h
