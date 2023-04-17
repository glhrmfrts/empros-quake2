#include "header/common.h"
#include "header/HandmadeMath.h"

enum { MAX_AREAS = 128 };

static void PushFreeArea(area_allocator_t* alloc, int left, int top, int right, int bottom)
{
	alloc->freeAreas = realloc(alloc->freeAreas, sizeof(allocator_area_t) * (++alloc->freeAreaCount));
	alloc->freeAreas[alloc->freeAreaCount - 1] = (allocator_area_t){left, top, right, bottom};
}

static void FreeAreaBuffer(area_allocator_t* alloc)
{
	if (alloc->freeAreas != NULL)
	{
		free(alloc->freeAreas);
		alloc->freeAreaCount = 0;
		alloc->freeAreas = NULL;
	}
}

static void RemoveFreeArea(area_allocator_t* alloc, int idx)
{
	if (alloc->freeAreaCount == 1)
	{
		FreeAreaBuffer(alloc);
		return;
	}
	allocator_area_t* newAreas = malloc(sizeof(allocator_area_t) * (alloc->freeAreaCount - 1));
	int newi = 0;
	for (int oldi = 0; oldi < alloc->freeAreaCount; oldi++)
	{
		if (oldi == idx)
		{
			continue;
		}
		newAreas[newi++] = alloc->freeAreas[oldi];
	}
	free(alloc->freeAreas);
	alloc->freeAreas = newAreas;
	alloc->freeAreaCount--;
}

static void CleanupAreas(area_allocator_t* alloc)
{
    // Remove rects which are contained within another rect
    for (size_t i = 0; i < alloc->freeAreaCount;)
    {
	qboolean erased = false;
	for (size_t j = i + 1; j < alloc->freeAreaCount;)
	{
	    if ((alloc->freeAreas[i].left >= alloc->freeAreas[j].left) &&
		(alloc->freeAreas[i].top >= alloc->freeAreas[j].top) &&
		(alloc->freeAreas[i].right <= alloc->freeAreas[j].right) &&
		(alloc->freeAreas[i].bottom <= alloc->freeAreas[j].bottom))
	    {
		RemoveFreeArea(alloc, i);
		erased = true;
		break;
	    }

	    if ((alloc->freeAreas[j].left >= alloc->freeAreas[i].left) &&
		(alloc->freeAreas[j].top >= alloc->freeAreas[i].top) &&
		(alloc->freeAreas[j].right <= alloc->freeAreas[i].right) &&
		(alloc->freeAreas[j].bottom <= alloc->freeAreas[i].bottom))
	    {
		RemoveFreeArea(alloc, j);
	    }
	    else
	    {
		j++;
	    }
	}
	if (!erased)
	{
	    i++;
	}
    }
}

static qboolean SplitArea(area_allocator_t* alloc, allocator_area_t original, allocator_area_t* reserve)
{
	if (reserve->right > original.left && reserve->left < original.right &&
		reserve->bottom > original.top && reserve->top < original.bottom)
	{
		// Check for splitting from the right
		if (reserve->right < original.right)
		{
			allocator_area_t newArea = original;
			newArea.left = reserve->right;
			PushFreeArea(alloc, newArea.left, newArea.top, newArea.right, newArea.bottom);
		}
		// Check for splitting from the left
		if (reserve->left > original.left)
		{
			allocator_area_t newArea = original;
			newArea.right = reserve->left;
			PushFreeArea(alloc, newArea.left, newArea.top, newArea.right, newArea.bottom);
		}
		// Check for splitting from the bottom
		if (reserve->bottom < original.bottom)
		{
			allocator_area_t newArea = original;
			newArea.top = reserve->bottom;
			PushFreeArea(alloc, newArea.left, newArea.top, newArea.right, newArea.bottom);
		}
		if (reserve->top < original.top)
		{
			allocator_area_t newArea = original;
			newArea.bottom = reserve->top;
			PushFreeArea(alloc, newArea.left, newArea.top, newArea.right, newArea.bottom);
		}
		return true;
	}
	return false;
}

void AreaAlloc_Init(area_allocator_t* alloc, int width, int height, int maxWidth, int maxHeight)
{
	FreeAreaBuffer(alloc);

	alloc->width = width;
	alloc->height = height;
	alloc->maxWidth = maxWidth;
	alloc->maxHeight = maxHeight;

	// Allocate a initial area
	PushFreeArea(alloc, 0, 0, width, height);
}

void AreaAlloc_Destroy(area_allocator_t* alloc)
{
	FreeAreaBuffer(alloc);
}

qboolean AreaAlloc_Allocate(area_allocator_t* alloc, int width, int height, int* x, int* y)
{
	if (width < 0)
		width = 0;
	if (height < 0)
		height = 0;

	int bestIndex;
	int bestFreeArea;

	for (;;)
	{
		bestIndex = -1;
		bestFreeArea = INT_MAX;
		for (int i = 0; i < alloc->freeAreaCount; i++)
		{
			int freeWidth = alloc->freeAreas[i].right - alloc->freeAreas[i].left;
			int freeHeight = alloc->freeAreas[i].bottom - alloc->freeAreas[i].top;
			if (freeWidth >= width && freeHeight >= height)
			{
				// Calculate rank for free area, lower is better
				int freeArea = freeWidth * freeHeight;
				if (freeArea < bestFreeArea)
				{
					bestIndex = i;
					bestFreeArea = freeArea;
				}
			}
		}

		if (bestIndex == -1)
		{
			if (alloc->doubleWidth && alloc->width < alloc->maxWidth)
			{
				int oldWidth = alloc->width;
				alloc->width <<= 1;
				// If no allocations yet, simply expand the single free area
				allocator_area_t* first = alloc->freeAreas;
				if (alloc->freeAreaCount == 1 &&
					first->left == 0 && first->top == 0 &&
					first->right == oldWidth && first->bottom == alloc->height)
				{
					PushFreeArea(alloc, oldWidth, 0, alloc->width, alloc->height);
				}
			}
			else if (!alloc->doubleWidth && alloc->height < alloc->maxHeight)
			{
				int oldHeight = alloc->height;
				alloc->height <<= 1;
				allocator_area_t* first = alloc->freeAreas;
				if (alloc->freeAreaCount == 1 &&
					first->left == 0 && first->top == 0 &&
					first->right == alloc->width && first->bottom == oldHeight)
				{
					PushFreeArea(alloc, 0, oldHeight, alloc->width, alloc->height);
				}
			}
			else
			{
				return false;
			}

			alloc->doubleWidth = !alloc->doubleWidth;
		}
		else
		{
			break;
		}
	}

	allocator_area_t* best = &alloc->freeAreas[bestIndex];
	*x = best->left;
	*y = best->top;

	allocator_area_t reserved = {best->left, best->top, best->left + width, best->top + height};

	// Remove the reserved area from all free areas
	for (size_t i = 0; i < alloc->freeAreaCount;)
	{
		if (SplitArea(alloc, alloc->freeAreas[i], &reserved))
		{
			RemoveFreeArea(alloc, i);
		}
		else
		{
			i++;
		}
	}

	CleanupAreas(alloc);

	return true;
}
