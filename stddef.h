#ifndef __STDDEF_H_
#define __STDDEF_H_

/* Minimal stddef.h for freestanding compilation */
typedef __SIZE_TYPE__ size_t;
#define NULL ((void*)0)
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* __STDDEF_H_ */
