#include "../src/UE.hpp"
#include <cassert>
#include <limits>
#include <cmath>
int main() {
    FMinimalViewInfo cam; FVector2D p;
    assert(worldToScreen(FVector(100,0,0),cam,400,800,p));
    assert(std::fabs(p.X-200)<.01 && std::fabs(p.Y-400)<.01);
    assert(worldToScreen(FVector(100,20,0),cam,400,800,p) && p.X>200);
    assert(worldToScreen(FVector(100,0,20),cam,400,800,p) && p.Y<400);
    assert(!worldToScreen(FVector(-100,0,0),cam,400,800,p));
    assert(!worldToScreen(FVector(100,0,0),cam,0,800,p));
    assert(zkNormYaw(540)==180 && zkNormYaw(-181)==179);
    assert(zkNormYaw(std::numeric_limits<float>::infinity())==0);
    cam.Rotation.Yaw=std::numeric_limits<float>::quiet_NaN();
    assert(!worldToScreen(FVector(100,0,0),cam,400,800,p));
}
