#pragma once
typedef void SSL;
typedef void SSL_CTX;
typedef void SSL_METHOD;
#define SSL_library_init() (void)0
#define SSL_load_error_strings() (void)0
#define OpenSSL_add_all_algorithms() (void)0
inline const void* TLS_client_method() { return nullptr; }
inline void* SSL_CTX_new(const void*) { return nullptr; }
inline void SSL_CTX_free(void*) {}
inline void* SSL_new(void*) { return nullptr; }
inline void SSL_free(void*) {}
inline int SSL_set_fd(void*, int) { return 0; }
inline int SSL_connect(void*) { return -1; }
inline int SSL_read(void*, void*, int) { return -1; }
inline int SSL_write(void*, const void*, int) { return -1; }
inline int SSL_shutdown(void*) { return 0; }
inline int SSL_get_error(const void*, int) { return 0; }
inline void SSL_CTX_set_verify(void*, int, void*) {}
inline int SSL_CTX_set_default_verify_paths(void*) { return 1; }
inline void SSL_set_tlsext_host_name(void*, const char*) {}
inline int SSL_pending(void*) { return 0; }
#define SSL_VERIFY_NONE 0
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_SYSCALL 5
#define SSL_ERROR_SSL 6
