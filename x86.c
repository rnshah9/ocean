#include "ast.h"
#include "types.h"

#include "rhd/heap_string.h"
#include <assert.h>

enum REGISTERS
{
    EAX,
    EBX,
    ECX,
    EDX,
    ESI,
    ESP,
    EBP,
    EIP
};

struct compile_context
{
	heap_string instr;
    int localsize;
};

static void dd(struct compile_context *ctx, u32 i)
{
    union
    {
        uint32_t i;
        uint8_t b[4];
    } u = { .i = i };
    
    for(size_t i = 0; i < 4; ++i)
		heap_string_push(&ctx->instr, u.b[i]);
}

static void dw(struct compile_context *ctx, u16 i)
{
    union
    {
        uint16_t s;
        uint8_t b[2];
    } u = { .s = i };

    heap_string_push(&ctx->instr, u.b[0]);
    heap_string_push(&ctx->instr, u.b[1]);
}

static void db(struct compile_context *ctx, u8 op)
{
    heap_string_push(&ctx->instr, op);
}

static void buf(struct compile_context *ctx, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
		heap_string_push(&ctx->instr, buf[i] & 0xff);
    }
}

static void process(struct compile_context *ctx, struct ast_node *n)
{
    switch(n->type)
    {
    case AST_PROGRAM:
        process(ctx, n->program_data.entry);
        break;
    case AST_LITERAL:        
        //mov eax,imm32
        db(ctx, 0xb8);
        dd(ctx, n->literal_data.integer);
        break;
    case AST_UNARY_EXPR:
    {
        struct ast_node *arg = n->unary_expr_data.argument;
        if(arg->type == AST_LITERAL)
        {
            switch(n->unary_expr_data.operator)
            {
            case '-':
                //neg eax
                //db(ctx, 0xf7);
                //db(ctx, 0xd8);
                
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, -arg->literal_data.integer);
                break;
                
            case '+':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, arg->literal_data.integer);
                break;

            case '!':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, !arg->literal_data.integer);
                break;
                
            case '~':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, ~arg->literal_data.integer);
                break;
                
            default:
                printf("unhandled unary expression %c\n", n->unary_expr_data.operator);
                break;
            }
        } else
        {
            process(ctx, arg);
            switch(n->unary_expr_data.operator)
            {
            case '-':
                //neg eax
                db(ctx, 0xf7);
                db(ctx, 0xd8);
                break;
            case '!':
            case '~':
                //not eax
                db(ctx, 0xf7);
                db(ctx, 0xd0);
                if(n->unary_expr_data.operator=='!')
                {
                    //and eax,1
					db(ctx, 0x83);
					db(ctx, 0xe0);
					db(ctx, 0x01);
                }
                break;
                
            default:
                printf("unhandled unary expression %c\n", n->unary_expr_data.operator);
                break;
            }
        }
    } break;
    
    case AST_BIN_EXPR:
    {
        struct ast_node *lhs = n->bin_expr_data.lhs;
        struct ast_node *rhs = n->bin_expr_data.rhs;

        //eax should still be stored leftover
        if(lhs->type == AST_LITERAL)
        {
            //mov eax,imm32
            db(ctx, 0xb8);
            dd(ctx, lhs->literal_data.integer);
        } else
            process(ctx, lhs);
        
        if(rhs->type == AST_LITERAL)
        {
            //mov ecx,imm32
            db(ctx, 0xb9);
            dd(ctx, rhs->literal_data.integer);
        } else
        {
            //push eax
            db(ctx, 0x50);
            process(ctx, rhs);
            //mov ecx,eax
            db(ctx, 0x89);
            db(ctx, 0xc1);
            //pop eax
            db(ctx, 0x58);
        }

        //xor edx,edx
        db(ctx, 0x31);
        db(ctx, 0xd2);

        //xor edx,edx
        //db(ctx, 0x31);
        //db(ctx, 0xd2);

        switch(n->bin_expr_data.operator)
        {
        case '*':
            //imul ecx
            db(ctx, 0xf7);
            db(ctx, 0xe9);
            break;
        case '/':
            //idiv ecx
            db(ctx, 0xf7);
            db(ctx, 0xf9);
            break;

        case '+':
            db(ctx, 0x01);
            db(ctx, 0xc8);
            break;
        case '-':
            db(ctx, 0x29);
            db(ctx, 0xc8);
            break;
        case '&':
            db(ctx, 0x21);
            db(ctx, 0xc8);
            break;
            break;
        case '|':
            db(ctx, 0x09);
            db(ctx, 0xc8);
            break;
        case '^':
            db(ctx, 0x31);
            db(ctx, 0xc8);
            break;

        default:
            printf("unhandled operator %c\n", n->bin_expr_data.operator);
            break;
        }

        //mov eax,edx
        //db(ctx, 0x89);
        //db(ctx, 0xd0);
            
    } break;

    case AST_ASSIGNMENT_EXPR:
    {
        //allocate some space

        //sub esp, 4
        db(ctx, 0x83);
        db(ctx, 0xec);
        db(ctx, 0x04);
        
        struct ast_node *lhs = n->assignment_expr_data.lhs;
        assert(lhs->type == AST_IDENTIFIER);
        
        struct ast_node *rhs = n->assignment_expr_data.rhs;
        process(ctx, rhs);
        //we should now have our result in eax
        
        switch(n->assignment_expr_data.operator)
        {
        case '=':
        {
            //lea ebx,[ebp-4]
            db(ctx, 0x8d);
            db(ctx, 0x5d);
            db(ctx, 0xfc - 4 * ctx->localsize++);

            //mov [ebx],eax
            db(ctx, 0x89);
            db(ctx, 0x03);
        } break;
        
        default:
            printf("unhandled assignment operator\n");
            break;
        }
        //TODO: cleanup local variables
    } break;
    
    default:
		printf("unhandled ast node type %d\n", n->type);
        break;
    }
}

heap_string x86(struct ast_node *head)
{
    struct compile_context ctx = {
		.instr = NULL,
        .localsize = 0
    };
    //push ebp
    //mov ebp, esp
    db(&ctx, 0x55);
    db(&ctx, 0x89);
    db(&ctx, 0xe5);
    process(&ctx, head);

    //mov esp,ebp
    //pop ebp
    db(&ctx, 0x89);
    db(&ctx, 0xec);
    db(&ctx, 0x5d);
    
    return ctx.instr;
}
