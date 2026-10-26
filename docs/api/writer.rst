Writer
======

A bounded append buffer that keeps counting after it runs out of room, so the same code can measure a message and
then write it into a buffer of exactly the right size.

.. code-block:: c

   lwhttp_buffer_t b;
   lwhttp_buffer_init(&b, NULL, 0);
   lwhttp_write_header(&b, "Accept", "*/*");
   lwhttp_write_end(&b);

   char *out = malloc(b.len);
   lwhttp_buffer_init(&b, out, b.len);
   lwhttp_write_header(&b, "Accept", "*/*");
   lwhttp_write_end(&b);

.. c:autostruct:: lwhttp_buffer_t
   :file: lwhttp.h
   :members:

.. c:autofunction:: lwhttp_buffer_init
   :file: lwhttp.h

.. c:autofunction:: lwhttp_buffer_append
   :file: lwhttp.h

.. c:autofunction:: lwhttp_buffer_append_str
   :file: lwhttp.h

.. c:autofunction:: lwhttp_buffer_append_u32
   :file: lwhttp.h

.. c:autofunction:: lwhttp_buffer_ok
   :file: lwhttp.h

.. c:autofunction:: lwhttp_write_header
   :file: lwhttp.h

.. c:autofunction:: lwhttp_write_end
   :file: lwhttp.h
