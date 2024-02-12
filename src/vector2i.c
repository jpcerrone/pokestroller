#include "vector2i.h"
#include <math.h>
struct Vector2i sum(struct Vector2i v0, struct Vector2i v1){
	struct Vector2i ret = {{v0.x + v1.x}, {v0.y + v1.y}};
	return ret;
}

struct Vector2i sub(struct Vector2i v0, struct Vector2i v1){
	struct Vector2i ret = {{v0.x - v1.x}, {v0.y + v1.y}};
	return ret;
}

float distanceSquared(struct Vector2i v0, struct Vector2i v1){
	int dy = (float)(v1.y - v0.y);
	int dx = (float)(v1.x - v0.x);
	return (float)(pow(dy, 2.0f) + pow(dx,2.0f ));


}

float distance(struct Vector2i v0, struct Vector2i v1){
	int dy = v1.y - v0.y;
	int dx = v1.x - v0.x;
	return sqrtf((pow(dy, 2.0f) + pow(dx,2.0f )));
}}
