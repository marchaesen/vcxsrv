LIBRARY=libcrypto

ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1 on the command line or as environment variable)
endif

DEFINES += SECURITY_WIN32
INCLUDES := .. ../windows $(INCLUDES)

CSRCS = \
  aes-common.c \
  aes-select.c \
  aes-sw.c \
  aesgcm-common.c \
  aesgcm-select.c \
  aesgcm-sw.c \
  aesgcm-ref-poly.c \
  arcfour.c \
  argon2.c \
  bcrypt.c \
  blake2.c \
  blowfish.c \
  chacha20-poly1305.c \
  crc32.c \
  des.c \
  diffie-hellman.c \
  dsa.c \
  ecc-arithmetic.c \
  ecc-ssh.c \
  hash_simple.c \
  hmac.c \
  mac.c \
  mac_simple.c \
  md5.c \
  mpint.c \
  ntru.c \
  openssh-certs.c \
  prng.c \
  pubkey-pem.c \
  pubkey-ppk.c \
  pubkey-ssh1.c \
  rsa.c \
  sha256-common.c \
  sha256-select.c \
  sha256-sw.c \
  sha512-common.c \
  sha512-select.c \
  sha512-sw.c \
  sha3.c \
  sha1-common.c \
  sha1-select.c \
  sha1-sw.c \
  xdmauth.c \
	rfc6979.c

