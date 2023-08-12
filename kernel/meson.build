project('Keyronex')

add_languages('c', 'cpp', 'objc')
port = get_option('port')

if (port == 'amd64')
	cpu = 'amd64'
	meson_cpu_family = 'x86_64'
	freestanding_c_args = [
		'-ffreestanding',
		'-fno-stack-protector',
		'-fno-stack-check',
		'-fno-strict-aliasing',
		'-fno-pie',
		'-fno-pic',
		'-m64',
		'-march=x86-64',
		'-mabi=sysv',
		'-mno-80387',
		'-mno-mmx',
		'-mno-sse',
		'-mno-sse2',
		'-mno-red-zone',
		'-mcmodel=kernel',
	]
elif (port == 'virt-m68k')
	cpu = 'm68k'
	meson_cpu_family = cpu
	freestanding_c_args = [
		'-ffreestanding',
		'-fno-stack-protector',
		'-fno-stack-check',
		'-fno-strict-aliasing',
		'-fno-pie',
		'-fno-pic',
		'-mcpu=68040',
	]
elif (port == 'virt-aarch64')
	cpu = 'aarch64'
	meson_cpu_family = cpu
	freestanding_c_args = [
		'-ffreestanding',
		'-fno-stack-protector',
		'-fno-stack-check',
		'-fno-strict-aliasing',
		'-fno-pie',
		'-fno-pic',
		'-mgeneral-regs-only',
	]
elif (port == 'soft')
	cpu = host_machine.cpu_family()
	meson_cpu_family = cpu
	freestanding_c_args = [
		'-DKRX_SOFT'
	]
else
	message('\n\tport ' + port + ' is not supported by Keyronex')
	error('\n\tbad port')
endif

freestanding_include_directories =[
	include_directories('../include', '../third_party/include')
]

kernel_sources = []

subdir('vm')

soft = executable('soft', 'test.c', kernel_sources,
	c_args: freestanding_c_args,
	include_directories: freestanding_include_directories
)
