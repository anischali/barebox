Release v2026.07.0
==================

CONFIG_CRYPTO_PUBLIC_KEYS
-------------------------

The ``fit-hint`` syntax in ``CONFIG_CRYPTO_PUBLIC_KEYS`` keyspecs was relaxed
to also accept a leading digit and ``.`` characters, e.g.
``fit-hint=1.3.6.1.4.1.4146.2.2``. Previously (since v2025.12.0) a hint had to
match ``[a-zA-Z][a-zA-Z0-9_-]*``; it now matches
``[a-zA-Z0-9_][a-zA-Z0-9_.-]*``. Existing hints remain valid, this is purely
an extension.

This is needed to register a public key under a dotted-decimal OID as its
name, which the new RFC 3161 timestamp verification support
(``CONFIG_CRYPTO_TS``) uses to look up the TSA key by the policy OID carried
in the timestamp token, e.g.::

  CONFIG_CRYPTO_PUBLIC_KEYS="keyring=tsa,fit-hint=1.3.6.1.4.1.4146.2.2:$(srctree)/keys/tsa.pem"