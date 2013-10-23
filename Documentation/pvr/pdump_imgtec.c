/*
 * Quick tool to convert an interleaved pdump log, with extra comments,
 * to the out2.txt and out2.prm files that Imagination Technologies can
 * run on its simulator.
 */

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#define __USE_GNU 1

#include <string.h>

char *script_name = "out2.txt";
char *param_name = "out2.prm";
FILE *script_file;
FILE *param_file;
int pdump_fd;
int pdump_size;
char *pdump_buf;

void
print_help(char *name)
{
	printf("Usage:\n");
	printf("%s <pdump file>\n", name);
}

int
out_open(void)
{
	script_file = fopen(script_name, "wx");
	if (!script_file) {
		fprintf(stderr, "Error: Failed to open %s: %s\n",
			script_name, strerror(errno));
		return errno;
	}

	param_file = fopen(param_name, "wx");
	if (!param_file) {
		fprintf(stderr, "Error: Failed to open %s: %s\n",
			param_name, strerror(errno));
		return errno;
	}

	return 0;
}

int
split_stream(void)
{
	char *current, *end = pdump_buf + pdump_size;
	char *tmp = NULL, *bin = NULL, *frame_end = NULL, *pid = NULL;
	char *last = NULL, *lastlast = NULL, *first = NULL, *hwrec = NULL;
	char *oldframe = NULL;
	size_t size, ret;
	long offset;

	lastlast = NULL;
	current = pdump_buf;
	do {
		tmp = memmem(current , end - current, "-- LASTONLY{", 12);
		if (tmp) {
			lastlast = tmp;
			current = tmp + 12;
		}
	} while (tmp);

	current = pdump_buf;

	/* trigger first memmems pass */
	bin = current - 1;
	frame_end = current - 1;
	last = current - 1;
	pid = current - 1;
	hwrec = current - 1;
	oldframe = current - 1;

	while (1) {
		if (bin && (bin < current))
			bin = memmem(current, end - current, "BIN 0x", 6);
		if (frame_end && (frame_end < current))
			frame_end = memmem(current, end - current,
					   "-- Ending current Frame", 23);
		if (last && (last < current))
			last = memmem(current, end - current,
					"-- LASTONLY{", 12);
		if (last && lastlast && (last == lastlast))
			last = NULL;
		if (pid && (pid < current))
			pid = memmem(current, end - current, "-- PID ", 7);
		if (hwrec && (hwrec < current))
			hwrec = memmem(current, end - current,
					"-- HW Recovery ", 15);
		if (oldframe && (oldframe < current))
			oldframe = memmem(current, end - current,
					"-- Starting Frame ", 18);

		first = bin;
		if (!first || (frame_end && (frame_end < first)))
			first = frame_end;
		if (!first || (last && (last < first)))
			first = last;
		if (!first || (pid && (pid < first)))
			first = pid;
		if (!first || (hwrec && (hwrec < first)))
			first = hwrec;

		if (first)
			size = first - current;
		else
			size = end - current;

		ret = fwrite(current, 1, size, script_file);
		if (ret != size) {
			fprintf(stderr, "Error: Failed to write to %s: %s\n",
				script_name, strerror(errno));
			return errno;
		}

		if (!first)
			return 0;

		current = first;

		if (current == bin) {
			tmp = memchr(current, ':', end - current);
			if (!tmp || (tmp != current + 14)) {
				fprintf(stderr,
					"Error: failed to parse BIN directive"
					" at 0x%08X\n", current - pdump_buf);
				return EIO;
			}

			size = strtoul(current + 4, NULL, 16);
			if (!size) {
				fprintf(stderr, "Error: failed to read BIN size"
					" at 0x%08X\n", current - pdump_buf);
				return EIO;
			}

			/* find end tag */
			if (((current + 15 + size + 12) > end) ||
			    strncmp(current + 15 + size,
				    "-- BIN END\r\n", 12)) {
				fprintf(stderr, "Error: failed to find BIN END "
					"directive at 0x%08X\n",
					current - pdump_buf);
				return EIO;
			}

			offset = ftell(param_file);

			/* write out BIN contents to param file */
			ret = fwrite(current + 15, 1, size, param_file);
			if (ret != size) {
				fprintf(stderr, "Error: Failed to write to %s:"
					" %s\n", param_name, strerror(errno));
				return errno;
			}

			current += 15 + size + 12;

			/* fix up the -- LDB line */
			tmp = memmem(current, end - current, "-- LDB ", 7);
			if (tmp == current) {
				tmp = memmem(current, end - current, "\r\n", 2);
				if (!tmp) {
					fprintf(stderr,
						"Error: failed to read LDB"
						" at 0x%08X\n",
						current - pdump_buf);
					return EIO;
				}

				ret = fwrite(current, 1, tmp - current,
						script_file);
				if (ret != (tmp - current)) {
					fprintf(stderr,
						"Error: Failed to write to %s:"
						" %s\n", script_name,
						strerror(errno));
					return errno;
				}

				current = tmp + 2;

				fprintf(script_file, " 0x%08lX %%0%%.prm\r\n",
						offset);
			}

			/* fix up the LDB lines */
			while (size > 0) {
				int block_size;

				tmp = memmem(current, end - current, "LDB ", 4);
				if (tmp != current)
					/* we might be hitting WRW */
					break;

				tmp = memmem(current, end - current, "\r\n", 2);
				if (!tmp || ((tmp - current) != 53)) {
					fprintf(stderr,
						"Error: failed to read LDB"
						" at 0x%08X\n",
						current - pdump_buf);
					return EIO;
				}

				if (sscanf(current + 43, "0x%08X",
							&block_size) != 1) {
					fprintf(stderr,
						"Error: failed to read LDB"
						"size at 0x%08X\n",
						current - pdump_buf);
					return EIO;
				}

				ret = fwrite(current, 1, 53, script_file);
				if (ret != 53) {
					fprintf(stderr,
						"Error: Failed to write to %s:"
						" %s\n", script_name,
						strerror(errno));
					return errno;
				}

				current = tmp + 2;

				fprintf(script_file, " 0x%08lX %%0%%.prm\r\n",
						offset);

				size -= block_size;
				offset += block_size;
			}
		} else if (current == frame_end) {
			/* skip "-- Ending current Frame\r\n" */
			tmp = memmem(current, end - current, "\r\n", 2);
			current = tmp + 2;
		} else if (current == pid) {
			/* skip "-- PID %d: Starting Frame %d\r\n" */
			tmp = memmem(current, end - current, "\r\n", 2);
			current = tmp + 2;
		} else if (current == hwrec) {
			/* skip "-- HW Recovery triggered by %d (%s)\r\n" */
			tmp = memmem(current, end - current, "\r\n", 2);
			current = tmp + 2;
		} else if (current == oldframe) {
			/* skip "-- Starting Frame %d\r\n" */
			tmp = memmem(current, end - current, "\r\n", 2);
			current = tmp + 2;
		} else if (current == last) {
			tmp = memmem(current, end - current, "-- }LASTONLY",
					12);
			current = tmp + 12 + 2;
		}
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	int ret;

	if (argc == 2) {
		if (argv[1][0] == '-') {
			print_help(argv[0]);
			return 0;
		}
	} else if (argc != 2) {
		fprintf(stderr, "Error: please only provide one argument.\n");
		print_help(argv[0]);
		return EINVAL;
	}

	pdump_fd = open(argv[1], O_RDONLY);
	if (pdump_fd < 0) {
		fprintf(stderr, "Error: failed to open %s: %s\n",
			argv[1], strerror(errno));
		return errno;
	}

	{
		struct stat buf;
		ret = stat(argv[1], &buf);
		if (ret) {
			fprintf(stderr, "Error: failed to fstat %s: %s\n",
				argv[1], strerror(errno));
			return errno;
		}

		pdump_size = buf.st_size;
		if (!pdump_size) {
			fprintf(stderr, "Error: %s is empty.\n", argv[1]);
			return 0;
		}
	}

	pdump_buf = mmap(NULL, pdump_size, PROT_READ, MAP_SHARED, pdump_fd, 0);
	if (((int) pdump_buf) == -1) {
		fprintf(stderr, "Error: failed to mmap %s: %s\n",
			argv[1], strerror(errno));
		return errno;
	}

	ret = out_open();
	if (ret)
		return ret;

	return split_stream();
}
