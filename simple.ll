; ModuleID = 'QuadReader'
source_filename = "QuadReader"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@0 = private unnamed_addr constant [21 x i8] c"x is greater than 1\0A\00", align 1

declare i32 @printf(i8*, ...)

declare void @exit(i32)

declare i32 @getchar()

define i32 @main() {
entry:
  %x = alloca i32, align 4
  store i32 2, i32* %x, align 4
  %t5 = load i32, i32* %x, align 4
  %t7 = icmp sgt i32 %t5, 1
  br i1 %t7, label %L1, label %L2

L1:                                               ; preds = %entry
  %t10 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([21 x i8], [21 x i8]* @0, i32 0, i32 0))
  br label %L2

L2:                                               ; preds = %L1, %entry
  ret i32 0
}
