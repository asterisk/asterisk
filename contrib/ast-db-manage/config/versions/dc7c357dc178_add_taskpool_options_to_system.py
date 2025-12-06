"""add taskpool options to system

Revision ID: dc7c357dc178
Revises: abdc9ede147d
Create Date: 2025-09-24 09:45:17.609185

"""

# revision identifiers, used by Alembic.
revision = 'dc7c357dc178'
down_revision = 'abdc9ede147d'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_systems', sa.Column('taskpool_minimum_size', sa.Integer))
    op.add_column('ps_systems', sa.Column('taskpool_initial_size', sa.Integer))
    op.add_column('ps_systems', sa.Column('taskpool_auto_increment', sa.Integer))
    op.add_column('ps_systems', sa.Column('taskpool_idle_timeout', sa.Integer))
    op.add_column('ps_systems', sa.Column('taskpool_max_size', sa.Integer))

def downgrade():
    op.drop_column('ps_systems', 'taskpool_minimum_size')
    op.drop_column('ps_systems', 'taskpool_initial_size')
    op.drop_column('ps_systems', 'taskpool_auto_increment')
    op.drop_column('ps_systems', 'taskpool_idle_timeout')
    op.drop_column('ps_systems', 'taskpool_max_size')
