#include "tests/main.h"
#include "tests/vm/parallel-merge.h"

void
test_main (void) 
{
  parallel_merge ("child-qsort", 72);
}

//exit_status 값이 72인 경우 자식 프로세스가 정상적으로 종료되지 않았음을 나타냅니다