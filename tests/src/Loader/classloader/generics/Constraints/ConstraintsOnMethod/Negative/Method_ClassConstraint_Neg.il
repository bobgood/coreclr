// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

.assembly extern mscorlib{}
.assembly extern types{}

.assembly Method_ClassConstraint_Neg {}



.class public auto ansi beforefieldinit B
         extends [mscorlib]System.Object
{

    // Generic method with class() constraint
    .method public hidebysig instance void 
            method1<class T>() cil managed
    {
      .maxstack  8
      ret
    }

    .method public hidebysig specialname rtspecialname 
            instance void  .ctor() cil managed
    {
      .maxstack  8
      ldarg.0
      call       instance void [mscorlib]System.Object::.ctor()
      ret
    } 
} 

.class public auto ansi beforefieldinit M_ClassConstraint 
         extends [mscorlib]System.Object
{

  // NEGATIVE TEST
  // generic argument is a struct
  .method public hidebysig static void Test3() cil managed
  {
    .maxstack  1
    
    newobj instance void class B::.ctor()
    call instance void class B::method1<valuetype [types]NonGenStruct>()
    ret
  }


  // NEGATIVE TEST
  // generic argument is an enum with no default ctor
  .method public hidebysig static void Test8() cil managed
  {
    .maxstack  1

    newobj instance void class B::.ctor()
    call instance void class B::method1<valuetype [types]Enum1>()
    ret
  }

}

.class public auto ansi beforefieldinit M_ClassConstraintGenTypes 
         extends [mscorlib]System.Object
{

  // NEGATIVE TEST
  // generic argument is a struct 
  .method public hidebysig static void Test3() cil managed
  {
    .maxstack  1
   
    newobj instance void class B::.ctor()
    call instance void class B::method1<valuetype [types]GenStruct<int32>>()

    ret
  }

  // NEGATIVE TEST
  // generic argument is NUllable<T>
  .method public hidebysig static void Test6() cil managed
  {
    .maxstack  1

    newobj instance void class B::.ctor()
    call instance void class B::method1<valuetype [mscorlib]System.'Nullable`1'<int32>>()

    ret
  }

}


