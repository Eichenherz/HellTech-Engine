
struct clear_entry
{
	uint bufferIdx;
	uint clearVal;
};


[[vk::push_constant]]
struct
{
	uint clearDescBufferIndex;
} pushBlock;


[[vk::binding( 0 )]] ByteAddressBuffer bufferTable[];

[numthreads(1, 1, 1)]
void Main( uint3 DTid : SV_DispatchThreadID )
{
	clear_entry ce = bufferTable[ clearDescBufferIndex ].Load<clear_entry>( DTid * sizeof( clear_entry ) );

}