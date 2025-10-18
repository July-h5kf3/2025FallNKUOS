#include<iostream>
using namespace std;

bool is_order_of_two(int n)
{
    //假设n为2的幂次，那么其二进制表示中只有一位为1，那么n-1将把除这一位的后续位数全变为1
    //那么n&(n-1)则必然为0
    return n>0 && n&(n-1)==0;
}

struct Buddy2
{
    int size;
    int longest[];
    
    //Buddy的构造函数，构造内存总单元数为n的分配器
    Buddy2(int n)
    {
        if ( n < 1 || !is_order_of_two(n) )
        {
            //假设n不为2的幂，那么就无法逐级折半
            return nullptr;
        }
        size = n;
        longest = new int[2*n];

        int node_size = 2 * n;
        for ( int i = 0; i < 2*n-1; i++)
        {
            if(is_order_of_two(i+1))
            {
                node_size/=2;
                //假设i+1为2的幂次，那么说明树进入了新的一层，node_size将折半
            }
            longest[i]=node_size;
        }        
    }

    //假设我们要分配大小为s的空间，我们使用Buddy2_alloc函数获取分配的内存空间的偏移量
    int Buddy2_alloc(int s)
    {
        if(s <= 0)
        {
            s = 1;
        }
        else if(!is_order_of_two(s))
        {
            //假设需要分配的内存单元不为2的幂，我们将其调整为大于s且为2的幂的大小，便于我们进行分配
            s = fix_size(s);
        }

        int index = 0;
        if(self->longest[index] < s)
        {
            //如果内存总单元也无法满足s需要的空间
            //为什么要用longest[index]，而不用size呢？
            //因为随着内存单元的不断分配，内存总空间会发生变化，我们需要当前情况下的总可分配空间
            return -1;
        }

        int node_size;
        int offset = 0;

        //寻找到合适的index，即可确认要分配的内存单元的索引
        //什么是合适的index呢？longest[index]=s时，这个内存单元恰好合适
        for(node_size = self->size;node_size=!s;node_size/=2)
        {
            if(self->longest[2*index+1]>=s)
            {
                //假设当前节点的左儿子所代表的内存空间满足s的要求
                //那么向左子树遍历
                index = 2 * index + 1;
            }
            else
            {
                //反正，如果左儿子的空间不能满足s
                //那么说明左儿子这部分的内存空间被占用了部分，无法满足s所需内存
                index = 2 * index + 2;
            }
        }

        //注意：这个循环的正确运行，建立在longest根据内存情况实时更新的前提下
        //假设s=512，此时longest[0]=512;longest[1]=0;longest[2]=512;
        //那么最终获取的Index为2
        
        self->longest[index] = 0;
        
        //offset的计算是这样的流程：
        //offset = node_size * pos，其中node_size = s，pos指当前index在该层的第几个
        //level = log_2{size / node_size} 
        //first_index = 2 ^ level - 1
        //pos = index - first_index
        //offset = node_size * (index - 2 ^ level + 1) = (index + 1) * node_size - size
        offset = (index + 1) * node_size - self->size;

        //和之前说的一样，一旦进行内存分配，就需要实时的更新longest[]
        //按照当前index，不断向上遍历，其父节点的值为左右儿子中的最大值
        while(index)
        {
            index = ( index - 1 ) / 2;
            self->longest[index] = Max(self->longest[2*index+1],self->longest[2*index+2]);
        } 

        return offset;
    }
}
