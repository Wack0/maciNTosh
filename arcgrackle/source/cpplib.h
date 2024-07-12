/**
 * You have been WARNED
 *
 * Many of the preprocessor metaprogramming techniques used here are from this
 * excellent article:
 * https://github.com/pfultz2/Cloak/wiki/C-Preprocessor-tricks,-tips,-and-idioms
 *
 * In general, it's best not to try to decipher this.
 */

#define CPP_PASTE(a,...) a ## __VA_ARGS__
#define CPP_CAT(a,...) CPP_PASTE(a,__VA_ARGS__)

#define CPP_ENUMERATE(x) x,
#define CPP_ID(x) x

#define CPP_EMPTY()
#define CPP_BLOCK_EXPANSION(x) x CPP_EMPTY()
#define CPP_DEFER(...) __VA_ARGS__ CPP_BLOCK_EXPANSION(CPP_EMPTY)()
#define CPP_EXPAND(...) __VA_ARGS__
#define CPP_EAT(...)

#define CPP_EVAL(...)  CPP_EVAL1(CPP_EVAL1(CPP_EVAL1(__VA_ARGS__)))
#define CPP_EVAL1(...) CPP_EVAL2(CPP_EVAL2(CPP_EVAL2(__VA_ARGS__)))
#define CPP_EVAL2(...) CPP_EVAL3(CPP_EVAL3(CPP_EVAL3(__VA_ARGS__)))
#define CPP_EVAL3(...) CPP_EVAL4(CPP_EVAL4(CPP_EVAL4(__VA_ARGS__)))
#define CPP_EVAL4(...) CPP_EVAL5(CPP_EVAL5(CPP_EVAL5(__VA_ARGS__)))
#define CPP_EVAL5(...) CPP_EXPAND(CPP_EXPAND(CPP_EXPAND(__VA_ARGS__)))

#define CPP_IF_(b) CPP_PASTE(CPP_IF__,b)
#define CPP_IF__0(t,...) __VA_ARGS__
#define CPP_IF__1(t,...) t

#define CPP_NEGATE(x) CPP_PASTE(CPP_NEGATE_,x)
#define CPP_NEGATE_0 1
#define CPP_NEGATE_1 0

 // Lord knows how this works or how I once understood it.
#define CPP_CHECK_N(x,n,...) n
#define CPP_CHECK(...) CPP_CHECK_N(__VA_ARGS__,0,)
#define CPP_PROBE(x) x, 1,
#define CPP_IS_PAREN_PROBE(...) CPP_PROBE(~)
#define CPP_IS_PAREN(x) CPP_CHECK(CPP_IS_PAREN_PROBE x)

#define CPP_NOT(x) CPP_CHECK(CPP_PASTE(CPP_NOT_,x))
#define CPP_NOT_0 CPP_PROBE(~)
#define CPP_AND(x) CPP_PASTE(CPP_AND_,x)
#define CPP_AND_0(y) 0
#define CPP_AND_1(y) y
#define CPP_OR(x) CPP_PASTE(CPP_OR_,x)
#define CPP_OR_0(y) y
#define CPP_OR_1(y) 1

#define CPP_IS_TRUTHY(x) CPP_NEGATE(CPP_NOT(x))
#define CPP_IF(pred) CPP_IF_(CPP_IS_TRUTHY(pred))

#define CPP_WHEN(pred) CPP_IF(pred)(CPP_EXPAND,CPP_EAT)
#define CPP_UNLESS(pred) CPP_IF(pred)(CPP_EAT,CPP_EXPAND)

#define CPP_NEQ_COMPARABLE(x,y) CPP_IS_PAREN(COMPARE_##x(COMPARE_##y)(()))
#define CPP_IS_COMPARABLE(x) CPP_IS_PAREN(CPP_CAT(COMPARE_,x)(()))
#define CPP_NOT_EQUAL(x,y) \
  CPP_IF_(CPP_AND(CPP_IS_COMPARABLE(x))(CPP_IS_COMPARABLE(y))) \
  (                                                            \
    CPP_NEQ_COMPARABLE,                                        \
    1 CPP_EAT                                                  \
  )(x,y)
#define CPP_EQUAL(x,y) CPP_NEGATE(CPP_NOT_EQUAL(x,y))

#define CPP_REPEAT_(n,f,...)      \
  CPP_WHEN(n)                    \
  (                              \
    CPP_DEFER(CPP_REPEAT__) ()  (CPP_DEC(n), f, __VA_ARGS__) \
    CPP_DEFER(f) (CPP_DEC(n), __VA_ARGS__)                   \
  )

#define CPP_REPEAT__() CPP_REPEAT_

#define CPP_REPEAT(...) CPP_EVAL(CPP_REPEAT_(__VA_ARGS__))

// Could use GCC's ,##__VA_ARGS__ extension to support 0 arguments.
#define CPP_N_VA_ARGS_(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,_20,_21,_22,_23,_24,_25,_26,_27,_28,_29,_30,_31,N,...) N
#define CPP_N_VA_ARGS(...) CPP_N_VA_ARGS_(__VA_ARGS__,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)

#define CPP_OVERLOAD(fn, ...) CPP_CAT(fn, CPP_N_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define CPP_FOREACH_0(f,x)
#define CPP_FOREACH_1(f,x) f(x)
#define CPP_FOREACH_2(f,x,...) f(x)CPP_FOREACH_1(f,__VA_ARGS__)
#define CPP_FOREACH_3(f,x,...) f(x)CPP_FOREACH_2(f,__VA_ARGS__)
#define CPP_FOREACH_4(f,x,...) f(x)CPP_FOREACH_3(f,__VA_ARGS__)
#define CPP_FOREACH_5(f,x,...) f(x)CPP_FOREACH_4(f,__VA_ARGS__)
#define CPP_FOREACH_6(f,x,...) f(x)CPP_FOREACH_5(f,__VA_ARGS__)
#define CPP_FOREACH_7(f,x,...) f(x)CPP_FOREACH_6(f,__VA_ARGS__)
#define CPP_FOREACH_8(f,x,...) f(x)CPP_FOREACH_7(f,__VA_ARGS__)
#define CPP_FOREACH_9(f,x,...) f(x)CPP_FOREACH_8(f,__VA_ARGS__)
#define CPP_FOREACH_10(f,x,...) f(x)CPP_FOREACH_9(f,__VA_ARGS__)
#define CPP_FOREACH_11(f,x,...) f(x)CPP_FOREACH_10(f,__VA_ARGS__)
#define CPP_FOREACH_12(f,x,...) f(x)CPP_FOREACH_11(f,__VA_ARGS__)
#define CPP_FOREACH_13(f,x,...) f(x)CPP_FOREACH_12(f,__VA_ARGS__)
#define CPP_FOREACH_14(f,x,...) f(x)CPP_FOREACH_13(f,__VA_ARGS__)
#define CPP_FOREACH_15(f,x,...) f(x)CPP_FOREACH_14(f,__VA_ARGS__)
#define CPP_FOREACH_16(f,x,...) f(x)CPP_FOREACH_15(f,__VA_ARGS__)
#define CPP_FOREACH_17(f,x,...) f(x)CPP_FOREACH_16(f,__VA_ARGS__)
#define CPP_FOREACH_18(f,x,...) f(x)CPP_FOREACH_17(f,__VA_ARGS__)
#define CPP_FOREACH_19(f,x,...) f(x)CPP_FOREACH_18(f,__VA_ARGS__)
#define CPP_FOREACH_20(f,x,...) f(x)CPP_FOREACH_19(f,__VA_ARGS__)
#define CPP_FOREACH_21(f,x,...) f(x)CPP_FOREACH_20(f,__VA_ARGS__)
#define CPP_FOREACH_22(f,x,...) f(x)CPP_FOREACH_21(f,__VA_ARGS__)
#define CPP_FOREACH_23(f,x,...) f(x)CPP_FOREACH_22(f,__VA_ARGS__)
#define CPP_FOREACH_24(f,x,...) f(x)CPP_FOREACH_23(f,__VA_ARGS__)
#define CPP_FOREACH_25(f,x,...) f(x)CPP_FOREACH_24(f,__VA_ARGS__)
#define CPP_FOREACH_26(f,x,...) f(x)CPP_FOREACH_25(f,__VA_ARGS__)
#define CPP_FOREACH_27(f,x,...) f(x)CPP_FOREACH_26(f,__VA_ARGS__)
#define CPP_FOREACH_28(f,x,...) f(x)CPP_FOREACH_27(f,__VA_ARGS__)
#define CPP_FOREACH_29(f,x,...) f(x)CPP_FOREACH_28(f,__VA_ARGS__)
#define CPP_FOREACH_30(f,x,...) f(x)CPP_FOREACH_29(f,__VA_ARGS__)
#define CPP_FOREACH_31(f,x,...) f(x)CPP_FOREACH_30(f,__VA_ARGS__)
#define CPP_FOREACH_32(f,x,...) f(x)CPP_FOREACH_31(f,__VA_ARGS__)

#define CPP_FOREACH(f,...) CPP_OVERLOAD(CPP_FOREACH_, f, __VA_ARGS__)



// No nicer way to write this. More cases can be added on an as-needed basis.
// Saturate instead of wraparound.
#define CPP_INC(x) CPP_PASTE(CPP_INC_,x)
#define CPP_INC_0 1
#define CPP_INC_1 2
#define CPP_INC_2 3
#define CPP_INC_3 4
#define CPP_INC_4 5
#define CPP_INC_5 6
#define CPP_INC_6 7
#define CPP_INC_7 8
#define CPP_INC_8 9
#define CPP_INC_9 10
#define CPP_INC_10 11
#define CPP_INC_11 12
#define CPP_INC_12 13
#define CPP_INC_13 14
#define CPP_INC_14 15
#define CPP_INC_15 16
#define CPP_INC_16 17
#define CPP_INC_17 18
#define CPP_INC_18 19
#define CPP_INC_19 20
#define CPP_INC_20 21
#define CPP_INC_21 22
#define CPP_INC_22 23
#define CPP_INC_23 24
#define CPP_INC_24 25
#define CPP_INC_25 26
#define CPP_INC_26 27
#define CPP_INC_27 28
#define CPP_INC_28 29
#define CPP_INC_29 30
#define CPP_INC_30 31
#define CPP_INC_31 32
#define CPP_INC_32 33
#define CPP_INC_33 34
#define CPP_INC_34 35
#define CPP_INC_35 36
#define CPP_INC_36 37
#define CPP_INC_37 38
#define CPP_INC_38 39
#define CPP_INC_39 40
#define CPP_INC_40 41
#define CPP_INC_41 42
#define CPP_INC_42 43
#define CPP_INC_43 44
#define CPP_INC_44 45
#define CPP_INC_45 46
#define CPP_INC_46 47
#define CPP_INC_47 48
#define CPP_INC_48 49
#define CPP_INC_49 50
#define CPP_INC_50 51
#define CPP_INC_51 52
#define CPP_INC_52 53
#define CPP_INC_53 54
#define CPP_INC_54 55
#define CPP_INC_55 56
#define CPP_INC_56 57
#define CPP_INC_57 58
#define CPP_INC_58 59
#define CPP_INC_59 60
#define CPP_INC_60 61
#define CPP_INC_61 62
#define CPP_INC_62 63
#define CPP_INC_63 63

#define CPP_DEC(x) CPP_PASTE(CPP_DEC_,x)
#define CPP_DEC_0 0
#define CPP_DEC_1 0
#define CPP_DEC_2 1
#define CPP_DEC_3 2
#define CPP_DEC_4 3
#define CPP_DEC_5 4
#define CPP_DEC_6 5
#define CPP_DEC_7 6
#define CPP_DEC_8 7
#define CPP_DEC_9 8
#define CPP_DEC_10 9
#define CPP_DEC_11 10
#define CPP_DEC_12 11
#define CPP_DEC_13 12
#define CPP_DEC_14 13
#define CPP_DEC_15 14
#define CPP_DEC_16 15
#define CPP_DEC_17 16
#define CPP_DEC_18 17
#define CPP_DEC_19 18
#define CPP_DEC_20 19
#define CPP_DEC_21 20
#define CPP_DEC_22 21
#define CPP_DEC_23 22
#define CPP_DEC_24 23
#define CPP_DEC_25 24
#define CPP_DEC_26 25
#define CPP_DEC_27 26
#define CPP_DEC_28 27
#define CPP_DEC_29 28
#define CPP_DEC_30 29
#define CPP_DEC_31 30
#define CPP_DEC_32 31
#define CPP_DEC_33 32
#define CPP_DEC_34 33
#define CPP_DEC_35 34
#define CPP_DEC_36 35
#define CPP_DEC_37 36
#define CPP_DEC_38 37
#define CPP_DEC_39 38
#define CPP_DEC_40 39
#define CPP_DEC_41 40
#define CPP_DEC_42 41
#define CPP_DEC_43 42
#define CPP_DEC_44 43
#define CPP_DEC_45 44
#define CPP_DEC_46 45
#define CPP_DEC_47 46
#define CPP_DEC_48 47
#define CPP_DEC_49 48
#define CPP_DEC_50 49
#define CPP_DEC_51 50
#define CPP_DEC_52 51
#define CPP_DEC_53 52
#define CPP_DEC_54 53
#define CPP_DEC_55 54
#define CPP_DEC_56 55
#define CPP_DEC_57 56
#define CPP_DEC_58 57
#define CPP_DEC_59 58
#define CPP_DEC_60 59
#define CPP_DEC_61 60
#define CPP_DEC_62 61
#define CPP_DEC_63 62
