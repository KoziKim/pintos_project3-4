#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* Doubly linked list.
*
* 이중 링크 리스트의 이러한 구현은 동적으로 할당된 메모리를 사용할 필요가 없습니다.  대신, 잠재적 목록 요소인 각 구조체
구조체 list_elem 멤버를 포함해야 합니다.
모든 리스트 함수는 이러한 `struct list_elem`에서 작동합니다.
list_entry 매크로를 사용하면 구조체 list_elem에서 이를 포함하는 구조체 객체로 다시 변환할 수 있습니다.

 * For example, suppose there is a needed for a list of `struct foo'.
   `struct foo' should contain a `struct list_elem' member, like so:

 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...other members...
 * };

 * Then a list of `struct foo' can be be declared and initialized like so:

 * struct list foo_list;

 * list_init (&foo_list);

 * 반복은 구조체 list_elem에서 다시 둘러싸는 구조체로 변환해야 하는 일반적인 상황입니다.
   Here's an example using foo_list:

 * struct list_elem *e;

 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 * e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...do something with f...
 * }

 * You can find real examples of list usage throughout the source;
  for example, malloc.c, palloc.c, and thread.c in the threads directory all use lists.

 * 이 목록의 인터페이스는 C++ STL의 list<> 템플릿에서 영감을 얻었습니다.
   list<>에 익숙하다면 쉽게 사용할 수 있을 것입니다.
   하지만 이 목록은 타입 검사를 '전혀' 하지 않으며 다른 많은 정확성 검사를 수행할 수 없다는 점을 강조해야 합니다.
   실수하면 물릴 수 있습니다.

 * Glossary of list terms:

 * - "front": The first element in a list.  
   Undefined in an empty list.
   Returned by list_front().

 * - "back": The last element in a list.  
   Undefined in an empty list.
   Returned by list_back().

 * - "꼬리": 비유적으로 목록의 마지막 요소 바로 뒤에 있는 요소입니다.  
   빈 리스트에서도 잘 정의됩니다.
   list_end()에 의해 반환됩니다.
   앞뒤로 반복할 때 마지막 센티널로 사용됩니다.

 * - "시작": 비어 있지 않은 목록에서는 앞부분입니다.  
   비어있는 리스트에서는 꼬리입니다.
   list_begin()에 의해 반환됩니다.  
   앞쪽에서 뒤쪽으로 반복하는 시작점으로 사용됩니다.

 * - "머리": 비유적으로 목록의 첫 번째 요소 바로 앞에 있는 요소입니다.
   빈 목록에서도 잘 정의됩니다.
   list_rend()에 의해 반환됩니다.  
   뒤에서 앞으로의 반복을 위한 마지막 센티널로 사용됩니다.

 * - "역방향 시작": 비어 있지 않은 목록에서는 뒤쪽입니다.  
   비어 있는 리스트에서는 머리입니다.
   list_rbegin()에 의해 반환됩니다.  
   뒤에서 앞으로의 반복을 위한 시작점으로 사용됩니다.

 * - "interior element": An element that is not the head or tail, that is, a real list element.  
   An empty list does not have any interior elements.*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* List element. */
struct list_elem {
	struct list_elem *prev;     /* Previous list element. */
	struct list_elem *next;     /* Next list element. */
};

/* List. */
struct list {
	struct list_elem head;      /* List head. */
	struct list_elem tail;      /* List tail. */
};

/* 리스트 엘리먼트 LIST_ELEM에 대한 포인터를 리스트 엘리먼트가 포함된 구조체에 대한 포인터로 변환합니다.
   외부 구조체의 이름 STRUCT와 목록 요소의 멤버 이름 MEMBER를 입력합니다.
   예제는 파일 상단의 큰 주석을 참조하세요. */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next	- offsetof (STRUCT, MEMBER.next)))

void list_init (struct list *);

/* List traversal. */
struct list_elem *list_begin (struct list *);
struct list_elem *list_next (struct list_elem *);
struct list_elem *list_end (struct list *);

struct list_elem *list_rbegin (struct list *);
struct list_elem *list_prev (struct list_elem *);
struct list_elem *list_rend (struct list *);

struct list_elem *list_head (struct list *);
struct list_elem *list_tail (struct list *);

/* List insertion. */
void list_insert (struct list_elem *, struct list_elem *);
void list_splice (struct list_elem *before, struct list_elem *first, struct list_elem *last);
void list_push_front (struct list *, struct list_elem *);
void list_push_back (struct list *, struct list_elem *);

/* List removal. */
struct list_elem *list_remove (struct list_elem *);
struct list_elem *list_pop_front (struct list *);
struct list_elem *list_pop_back (struct list *);

/* List elements. */
struct list_elem *list_front (struct list *);
struct list_elem *list_back (struct list *);

/* List properties. */
size_t list_size (struct list *);
bool list_empty (struct list *);

/* Miscellaneous. */
void list_reverse (struct list *);

/* 보조 데이터 AUX가 주어졌을 때 두 목록 요소 A와 B의 값을 비교합니다.
   A가 B보다 작으면 참을 반환하고, A가 B보다 크거나 같으면 거짓을 반환합니다. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

/* Operations on lists with ordered elements. */
void list_sort (struct list *,
                list_less_func *, void *aux);
void list_insert_ordered (struct list *, struct list_elem *,list_less_func *, void *aux);
void list_unique (struct list *, struct list *duplicates,
                  list_less_func *, void *aux);

/* Max and min. */
struct list_elem *list_max (struct list *, list_less_func *, void *aux);
struct list_elem *list_min (struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
