"""add reason_paused to queue_members

Revision ID: 4042a0ff4d9f
Revises: f261363a857f
Create Date: 2022-12-21 14:24:48.885750

"""

# revision identifiers, used by Alembic.
revision = '4042a0ff4d9f'
down_revision = 'f261363a857f'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('queue_members', sa.Column('reason_paused', sa.String(80)))


def downgrade():
    op.drop_column('queue_members', 'reason_paused')
