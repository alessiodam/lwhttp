Header store
============

Stores header fields back to back as NUL-terminated name and value pairs in a caller-supplied buffer. Lookups are
case-insensitive and return the first match.

.. c:autostruct:: lwhttp_headers_t
   :file: lwhttp.h
   :members:

.. c:autofunction:: lwhttp_headers_init
   :file: lwhttp.h

.. c:autofunction:: lwhttp_headers_clear
   :file: lwhttp.h

.. c:autofunction:: lwhttp_headers_add
   :file: lwhttp.h

.. c:autofunction:: lwhttp_headers_get
   :file: lwhttp.h

.. c:autofunction:: lwhttp_headers_at
   :file: lwhttp.h

.. c:autofunction:: lwhttp_headers_count
   :file: lwhttp.h
