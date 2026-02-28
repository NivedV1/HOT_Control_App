/****************************************************************************
** Resource object code
**
** Created by: The Resource Compiler for Qt version 6.10.2
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#ifdef _MSC_VER
// disable informational message "function ... selected for automatic inline expansion"
#pragma warning (disable: 4711)
#endif

static const unsigned char qt_resource_data[] = {
  // theme.qss
  0x0,0x0,0x0,0x40,
  0x2f,
  0x2a,0x20,0x51,0x74,0x20,0x44,0x61,0x72,0x6b,0x20,0x54,0x68,0x65,0x6d,0x65,0x20,
  0x53,0x74,0x79,0x6c,0x65,0x73,0x68,0x65,0x65,0x74,0x20,0x2d,0x20,0x57,0x65,0x20,
  0x77,0x69,0x6c,0x6c,0x20,0x6d,0x6f,0x76,0x65,0x20,0x6f,0x75,0x72,0x20,0x43,0x53,
  0x53,0x20,0x68,0x65,0x72,0x65,0x20,0x6c,0x61,0x74,0x65,0x72,0x20,0x2a,0x2f,
  
};

static const unsigned char qt_resource_name[] = {
  // theme.qss
  0x0,0x9,
  0xc,0x38,0xad,0x63,
  0x0,0x74,
  0x0,0x68,0x0,0x65,0x0,0x6d,0x0,0x65,0x0,0x2e,0x0,0x71,0x0,0x73,0x0,0x73,
  
};

static const unsigned char qt_resource_struct[] = {
  // :
  0x0,0x0,0x0,0x0,0x0,0x2,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x1,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
  // :/theme.qss
  0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1,0x0,0x0,0x0,0x0,
0x0,0x0,0x1,0x9c,0xa2,0xa7,0xfe,0x64,

};

#ifdef QT_NAMESPACE
#  define QT_RCC_PREPEND_NAMESPACE(name) ::QT_NAMESPACE::name
#  define QT_RCC_MANGLE_NAMESPACE0(x) x
#  define QT_RCC_MANGLE_NAMESPACE1(a, b) a##_##b
#  define QT_RCC_MANGLE_NAMESPACE2(a, b) QT_RCC_MANGLE_NAMESPACE1(a,b)
#  define QT_RCC_MANGLE_NAMESPACE(name) QT_RCC_MANGLE_NAMESPACE2( \
        QT_RCC_MANGLE_NAMESPACE0(name), QT_RCC_MANGLE_NAMESPACE0(QT_NAMESPACE))
#else
#   define QT_RCC_PREPEND_NAMESPACE(name) name
#   define QT_RCC_MANGLE_NAMESPACE(name) name
#endif

#if defined(QT_INLINE_NAMESPACE)
inline namespace QT_NAMESPACE {
#elif defined(QT_NAMESPACE)
namespace QT_NAMESPACE {
#endif

bool qRegisterResourceData(int, const unsigned char *, const unsigned char *, const unsigned char *);
bool qUnregisterResourceData(int, const unsigned char *, const unsigned char *, const unsigned char *);

#ifdef QT_NAMESPACE
}
#endif

int QT_RCC_MANGLE_NAMESPACE(qInitResources_resources)();
int QT_RCC_MANGLE_NAMESPACE(qInitResources_resources)()
{
    int version = 3;
    QT_RCC_PREPEND_NAMESPACE(qRegisterResourceData)
        (version, qt_resource_struct, qt_resource_name, qt_resource_data);
    return 1;
}

int QT_RCC_MANGLE_NAMESPACE(qCleanupResources_resources)();
int QT_RCC_MANGLE_NAMESPACE(qCleanupResources_resources)()
{
    int version = 3;
    QT_RCC_PREPEND_NAMESPACE(qUnregisterResourceData)
       (version, qt_resource_struct, qt_resource_name, qt_resource_data);
    return 1;
}

#ifdef __clang__
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif

namespace {
   struct initializer {
       initializer() { QT_RCC_MANGLE_NAMESPACE(qInitResources_resources)(); }
       ~initializer() { QT_RCC_MANGLE_NAMESPACE(qCleanupResources_resources)(); }
   } dummy;
}

#ifdef __clang__
#   pragma clang diagnostic pop
#endif
