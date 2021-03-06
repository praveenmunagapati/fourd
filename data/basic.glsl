// basic
#version 330

struct Vertex 
{
	float4 position : POSITION;
	float4 color	: COLOR0;
};

struct VOut
{
	float4 HPos	: POSITION;
	float4 Col0	: COLOR0;
	float2 Tex0 : TEXCOORDS0;
};

VOut main(Vertex IN, uniform float4x4 worldMatrix, uniform float4 worldPosition,
	uniform float4 cameraPosition, uniform float4x4 cameraMatrix,
	uniform float4x4 projectionMatrix, uniform float4x4 fourToThree,
	uniform float4 wPlaneNearFar)
{
	VOut OUT;
	//float4 worldSpace = mul(worldMatrix, float4(IN.position.xyz, 1.0)); // rotation/scale in 4d around origin
    float4 worldSpace = mul(worldMatrix, IN.position); // rotation/scale in 4d around origin
	worldSpace += worldPosition; // final world space position

	OUT.Tex0.x = IN.position.x + IN.position.z;
	OUT.Tex0.y = IN.position.y + IN.position.w;
	
	float4 cameraSpace = worldSpace - cameraPosition; // translate to be around camera origin but not transformed
	cameraSpace = mul(cameraMatrix, cameraSpace); // final camera space position
	
	float4 threeSpace = mul(fourToThree, cameraSpace);
	float fourProjectionScalar = (wPlaneNearFar.y - threeSpace.w) / (wPlaneNearFar.y - wPlaneNearFar.x);
    // TODO: (make a smoother depth calc that doesn't go through a zero singularity)
	//fourProjectionScalar = clamp(fourProjectionScalar, 0.0, 1.0);
	threeSpace.xy = lerp(threeSpace.xy, threeSpace.xy * fourProjectionScalar, wPlaneNearFar.z);
	//threeSpace.xy *= clamp(threeSpace.w, 0.5, 1.0);
    //threeSpace.xy *= threeSpace.w;
    //threeSpace.xyz *= IN.position.w; // clamp(IN.position.w, 0.5, 1.0);
    float savedW = threeSpace.w;
	threeSpace.w = 1;
	//threeSpace = cameraSpace;
	
	float4 homogenousCoords = mul(projectionMatrix, threeSpace); // homogenous clip space position
	//homogenousCoords.w = abs(homogenousCoords.w + savedW);
	//homogenousCoords.z = abs(homogenousCoords.z);
	OUT.HPos = homogenousCoords;
	//OUT.HPos = mul(megaMatrix, IN.position);
	//OUT.HPos = mul(megaMatrix, worldSpace);
	
    OUT.Col0.a = 0.2;
    OUT.Col0.g = IN.color.y;
    //OUT.Col0.r = abs(worldSpace.z / 10);
    //OUT.Col0.b = abs(worldSpace.w / 10);
    OUT.Col0.r = abs(savedW / 10);
    OUT.Col0.b = abs(threeSpace.x / 10);
	//OUT.Col0.xyz = IN.color.xyz;
	//OUT.Col0.xyz = float3(savedW, savedW, savedW);
	//SOUT.Col0.xyz = IN.position.xyz;
	//OUT.Col0.xyz = worldSpace.xyz;
	//OUT.Col0.xyz = homogenousCoords.xyz;
	//OUT.Col0.xyz = OUT.HPos.xyz;
	OUT.Col0.rgb = max(OUT.Col0.rgb, float3(0,0,0));
	OUT.Col0.rgb += float3(0.1,0.1,0.1);
	//OUT.Col0.r = IN.position.w;
	
	
	return OUT;
}
