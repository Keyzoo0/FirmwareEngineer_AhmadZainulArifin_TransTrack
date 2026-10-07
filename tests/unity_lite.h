/* Tiny assertion helpers for host unit tests (no external dependencies). */
#ifndef UNITY_LITE_H
#define UNITY_LITE_H
#include <stdio.h>
#include <stdlib.h>

static int g_failures, g_checks;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); g_checks++; \
    if (_a != _b) { g_failures++; printf("  FAIL %s:%d: %s == %lld, expected %lld\n", \
    __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_NEAR(a, b, tol) do { long long _a = (long long)(a), _b = (long long)(b); g_checks++; \
    if (llabs(_a - _b) > (tol)) { g_failures++; printf("  FAIL %s:%d: %s == %lld, expected %lld +/- %d\n", \
    __FILE__, __LINE__, #a, _a, _b, (int)(tol)); } } while (0)
#define RUN(test) do { printf("- %s\n", #test); test(); } while (0)
#define REPORT() (printf("%d checks, %d failures\n", g_checks, g_failures), g_failures ? 1 : 0)
#endif
