#include <stdio.h>
#include <stdlib.h>

#define MAXNUM 500 // 최대 갯수

struct stk
{
    int ID;
    int num;
    int price;
};

int main(void)
{
    FILE *fp;
    int ID, num, price, idx;
    int cnt, stk_num;

    struct stk list[MAXNUM];
    char isIn[MAXNUM] = {0};

    fp = fopen("stock.txt", "w");
    
    while(1)
    {
        printf("Input number of stocks ( 1 ~ %d ) : ", MAXNUM);
        scanf("%d", &stk_num);
        if(stk_num <= MAXNUM)
            break;
        printf("Number too high\n");
    }
    cnt = stk_num;
    while(cnt)
    {
        idx = rand() % stk_num;
        if(isIn[idx])
            continue;
        isIn[idx] = 1;
        list[stk_num - cnt].ID = idx;
        list[stk_num - cnt].price = ((rand() % 100) + 1) * 500;
        list[stk_num - cnt].num = (rand() % 95) + 5;
        cnt--;
    }
    for (int i=0; i<stk_num; i++)
    {
        fprintf(fp, "%d %d %d\n", list[i].ID+1, list[i].num, list[i].price);
    }
    fclose(fp);

    return 0;
}
