#include "physics_attitude.h"
#include "physics_velocity.h"
#include "physics_vector.h"
#include "frame_math.h"
#include "reference_io.h"

static void vector(int32_t *v,unsigned n) {unsigned i;for (i=0;i<n;i++) v[i]=oracle_signed(oracle_require());}
static void emit(const int32_t *v,unsigned n) {unsigned i;for (i=0;i<n;i++) oracle_put((uint32_t)v[i]);}
static unsigned char ram[0x540000];
static int read_data(void *ctx,uint32_t at,void *out,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(out,ram+at,n);return 1;}
static int write_data(void *ctx,uint32_t at,const void *data,uint32_t n)
{(void)ctx;if (at>sizeof(ram)||n>sizeof(ram)-at) return 0;memcpy(ram+at,data,n);return 1;}
int main(void)
{
    uint32_t op;const physics_trig *trig=frame_math_trig();oracle_binary();fwrite("MA01",1,4,stdout);
    while (oracle_word(&op)) {
        int32_t m[9],a[3],b[3],out[3];uint32_t angle,i;int status;
        switch (op) {
        case 1:
            angle=oracle_require();oracle_put((uint32_t)trig->sine[(angle&65532u)>>2]);oracle_put((uint32_t)trig->cosine[(angle&65532u)>>2]);break;
        case 2: {
            int32_t result=0x12345678;vector(a,2);status=physics_angle_lookup(a[0],a[1],trig,&result);oracle_put((uint32_t)status);oracle_put((uint32_t)result);break;
        }
        case 3: {
            uint32_t angles[3];for (i=0;i<3;i++) angles[i]=oracle_require();physics_matrix_from_angles(m,angles,trig);emit(m,9);break;
        }
        case 4:
            vector(m,9);i=oracle_require();angle=oracle_require();physics_matrix_rotate(m,i,angle,trig);emit(m,9);break;
        case 5:
            vector(m,9);status=physics_matrix_normalize(m);oracle_put((uint32_t)status);emit(m,9);break;
        case 6: {
            int32_t pivot[3],angles[3];vector(m,9);vector(a,3);vector(pivot,3);vector(angles,3);
            physics_pivot_rotate(m,a,pivot,angles,trig);emit(m,9);emit(a,3);break;
        }
        case 7: {
            physics_attitude_state s;physics_attitude_config c;int32_t dt;uint32_t frame;
            vector(s.reference,9);vector(s.matrix,9);vector(s.position,3);vector(s.angular,3);
            s.longitudinal_delta=oracle_signed(oracle_require());s.lateral_delta=oracle_signed(oracle_require());vector(s.pivot,3);
            c.longitudinal_multiplier=oracle_require();c.longitudinal_shift=oracle_require();c.lateral_multiplier=oracle_require();c.lateral_shift=oracle_require();
            dt=oracle_signed(oracle_require());frame=oracle_require();status=physics_attitude_step(&s,&c,dt,frame,trig);
            oracle_put((uint32_t)status);emit(s.reference,9);emit(s.matrix,9);emit(s.position,3);emit(s.angular,3);
            oracle_put((uint32_t)s.longitudinal_delta);oracle_put((uint32_t)s.lateral_delta);emit(s.pivot,3);break;
        }
        case 8: {
            int inverse,large;vector(a,3);vector(m,9);inverse=(int)oracle_require();large=(int)oracle_require();
            physics_velocity_transform(a,m,b,inverse,large);emit(b,3);oracle_put((uint32_t)physics_velocity_large(a));break;
        }
        case 9:vector(a,3);physics_vector_normalize(a);emit(a,3);break;
        case 10:vector(a,3);vector(b,3);physics_vector_cross(a,b,out);emit(out,3);break;
        case 11:vector(a,3);angle=oracle_require();physics_vector_yaw(a,out,angle,trig);emit(out,3);break;
        case 12: {
            uint32_t object=0x10000;vector(a,3);
            oracle_set(ram+object+0xe4,(uint32_t)a[0],4);oracle_set(ram+object+0xec,(uint32_t)a[2],4);
            oracle_set(ram+object+0xcc,oracle_require(),4);oracle_set(ram+object+0xb8,oracle_require(),4);
            for (i=0;i<3;i++) oracle_set(ram+0x531c98+i*4,oracle_require(),4);
            oracle_set(ram+object+0x686,oracle_require(),4);oracle_set(ram+object+0x68a,oracle_require(),4);
            status=physics_yaw_correction(read_data,write_data,0,object);oracle_put((uint32_t)status);oracle_put(oracle_get(ram+object+0xb8,4));break;
        }
        case 13:vector(a,2);oracle_put((uint32_t)physics_length2_approx(a[0],a[1]));break;
        default:return 2;
        }
    }
    return ferror(stdin) ? 3 : 0;
}
