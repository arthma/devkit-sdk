# PnP Client C SDK header files

This folder contains the public and internal header files for the PnP Client for the C SDK.  See [the main PnP reference document](../readme.md#publicHeaders) for additional policy information about these headers.

The `MOCKABLE_FUNCTION` references throughout the header files are used by PnP's unit testing framework.  PnP is [extensively tested](../tests).  For production code (which is the default unless you're building a unit test), the MOCKABLE_FUNCTION effectively becomes a no-op which just produces a standard C function declaration.