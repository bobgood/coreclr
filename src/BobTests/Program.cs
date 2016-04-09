using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;

namespace BobTests
{
    class Test
    {
        public byte y = 99;
    }

    class Program

        static void Main(string[] args)
        {

            AllocReset();
            AllocPushNew();
         //   var n = new Test();
            //var t=GC.GetTotalMemory(false);
            ////System.Threading.Tasks.TaskTimeoutExtensions();
            //Task t1 = new Task(() => { Console.WriteLine("Task"); Debugger.Break(); });
            //t1.Start();
            //t1.Wait();


            Console.WriteLine("Hello Worlk");
            Debugger.Break();
        }


        // 1 = reset to GCHeap
        // 2 = push new arena allocator
        // 3 = push GCHeap
        // 4 = pop
        static void AllocReset()
        {
            GC.AddMemoryPressure(1);
        }
        static void AllocPushNew()
        {
            GC.AddMemoryPressure(2);
        }
        static void AllocPushGCHeap()
        {
            GC.AddMemoryPressure(3);
        }
        static void AllocPop()
        {
            GC.AddMemoryPressure(4);
        }
    }
}
