Client
======

The client runs one request at a time over a kept-alive connection. See the :doc:`guide <../guide/async>` for the
event model.

Types
-----

.. c:autotype:: lwhttp_client_t
   :file: lwhttp.h

.. c:autostruct:: lwhttp_client_config_t
   :file: lwhttp.h
   :members:

.. c:autostruct:: lwhttp_request_t
   :file: lwhttp.h
   :members:

.. c:autotype:: lwhttp_body_reader_t
   :file: lwhttp.h

.. c:autostruct:: lwhttp_client_handlers_t
   :file: lwhttp.h
   :members:

.. c:autostruct:: lwhttp_header_t
   :file: lwhttp.h
   :members:

.. c:autostruct:: lwhttp_client_stats_t
   :file: lwhttp.h
   :members:

.. c:automacro:: LWHTTP_LENGTH_UNKNOWN
   :file: lwhttp.h

Lifetime
--------

.. c:autofunction:: lwhttp_client_config_default
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_create
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_destroy
   :file: lwhttp.h

Requests
--------

.. c:autofunction:: lwhttp_client_request
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_wait
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_request_sync
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_cancel
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_close
   :file: lwhttp.h

Results
-------

.. c:autofunction:: lwhttp_client_is_done
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_result
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_status
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_version
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_header
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_headers
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_url
   :file: lwhttp.h

.. c:autofunction:: lwhttp_client_stats
   :file: lwhttp.h

Upgrades
--------

.. c:autofunction:: lwhttp_client_detach
   :file: lwhttp.h
